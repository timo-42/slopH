#include "syntax_internal.h"

#include <ctype.h>
#include <stdalign.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>

typedef struct Token { const char *text; size_t length, start, end; } Token;
typedef struct Vector { void *data; size_t count, capacity, item_size; } Vector;
typedef struct Parser {
    SlophContext *context; SlophSyntaxModule *module; const unsigned char *source;
    size_t source_length; Token *tokens; size_t token_count, index, depth;
    SlophStatus status;
} Parser;

static SlophSpan span_at(size_t start, size_t end) { SlophSpan s = {start, end}; return s; }
static const SlophAllocator *pa(Parser *p) { return sloph_context_allocator(p->context); }
static void vector_destroy(Parser *p, Vector *v) {
    if (v->data != NULL) pa(p)->deallocate(pa(p)->user_data, v->data, v->capacity * v->item_size);
    memset(v, 0, sizeof(*v));
}
static bool vector_push(Parser *p, Vector *v, const void *item, size_t size) {
    void *grown; size_t capacity;
    if (p->status != SLOPH_STATUS_OK) return false;
    if (v->item_size == 0u) v->item_size = size;
    if (v->item_size != size) { p->status = SLOPH_STATUS_INTERNAL_ERROR; return false; }
    if (v->count == v->capacity) {
        capacity = v->capacity == 0u ? 4u : v->capacity * 2u;
        if (capacity < v->capacity || capacity > SIZE_MAX / size) { p->status = SLOPH_STATUS_LIMIT_EXCEEDED; return false; }
        grown = pa(p)->resize(pa(p)->user_data, v->data, v->capacity * size, capacity * size);
        if (grown == NULL) { p->status = SLOPH_STATUS_OUT_OF_MEMORY; return false; }
        v->data = grown; v->capacity = capacity;
    }
    memcpy((unsigned char *)v->data + v->count * size, item, size); v->count++;
    return true;
}
static void *vector_finish(Parser *p, Vector *v, size_t alignment) {
    void *result = NULL;
    if (p->status == SLOPH_STATUS_OK && v->count != 0u) {
        p->status = sloph_syntax_alloc(p->module, v->count * v->item_size, alignment, &result);
        if (p->status == SLOPH_STATUS_OK) memcpy(result, v->data, v->count * v->item_size);
    }
    vector_destroy(p, v); return result;
}
static void error(Parser *p, const char *code, const char *message, SlophSpan span) {
    if (p->status == SLOPH_STATUS_OK)
        p->status = sloph_syntax_diagnostic(p->context, code, "parse", message, span, SLOPH_STATUS_INVALID_ARGUMENT);
}
static void error_token(Parser *p, const char *message, const Token *token) {
    char details[512];
    if (p->status != SLOPH_STATUS_OK) return;
    if (token->length > 400u) {
        error(p, "syntax.parse.unexpected_token", message,
              span_at(token->start, token->end));
        return;
    }
    (void)snprintf(details, sizeof(details), "{\"token\":\"%.*s\"}",
                   (int)token->length, token->text);
    p->status = sloph_context_add_diagnostic_full(
        p->context, "syntax.parse.unexpected_token", "parse", message,
        details, span_at(token->start, token->end), SLOPH_SEVERITY_ERROR);
    if (p->status == SLOPH_STATUS_OK) p->status = SLOPH_STATUS_INVALID_ARGUMENT;
}
static void limit(Parser *p, const char *name, size_t configured, SlophSpan span) {
    char message[160];
    (void)snprintf(message, sizeof(message), "%s limit exceeded (configured %zu)", name, configured);
    if (p->status == SLOPH_STATUS_OK)
        p->status = sloph_syntax_diagnostic(p->context, "syntax.parse.limit_exceeded", "parse", message, span, SLOPH_STATUS_LIMIT_EXCEEDED);
}
static bool token_is(const Token *t, const char *text) { size_t n = strlen(text); return t->length == n && memcmp(t->text, text, n) == 0; }
static bool peek(Parser *p, const char *text) { return token_is(&p->tokens[p->index], text); }
static Token *take(Parser *p, const char *expected) {
    Token *token = &p->tokens[p->index];
    if (p->status != SLOPH_STATUS_OK) return token;
    if (expected != NULL && !token_is(token, expected)) { error(p, "syntax.parse.unexpected_token", "unexpected token", span_at(token->start, token->end)); return token; }
    p->index++; return token;
}
static bool keyword(const Token *t) {
    static const char *const words[] = {"module","import","when","is","public","type","fn","value","const","let","case","primitive","intrinsic","foreign","owned","own","borrow","defer"};
    size_t i; for (i=0u;i<sizeof(words)/sizeof(words[0]);i++) if (token_is(t, words[i])) return true; return false;
}
static bool ident_token(const Token *t) {
    size_t i; if (t->length == 0u || keyword(t) || !(isalpha((unsigned char)t->text[0]) || t->text[0]=='_')) return false;
    for (i=1u;i<t->length;i++)
        if (!(isalnum((unsigned char)t->text[i]) || t->text[i]=='_')) return false;
    return true;
}
static Token *ident(Parser *p, bool upper, const char *role) {
    Token *t = &p->tokens[p->index]; (void)role;
    if (!ident_token(t) || (upper ? !isupper((unsigned char)t->text[0]) : !(islower((unsigned char)t->text[0]) || t->text[0]=='_'))) {
        error(p, "syntax.parse.invalid_name", "invalid source name", span_at(t->start,t->end)); return t;
    }
    p->index++; return t;
}
static char *copy_token(Parser *p, const Token *t) { char *s=NULL; if (p->status==SLOPH_STATUS_OK) p->status=sloph_syntax_string(p->module,t->text,t->length,&s); return s; }
static char *parse_path(Parser *p, SlophSpan *out_span) {
    Token *first = &p->tokens[p->index]; size_t start=first->start,end=first->end; SlophBuffer b; Token *part;
    sloph_buffer_init(&b,p->context,sloph_context_limits(p->context)->token_bytes * 16u);
    if (!ident_token(first)) error(p,"syntax.parse.unexpected_token","expected identifier",span_at(first->start,first->end));
    while (p->status==SLOPH_STATUS_OK) {
        part=take(p,NULL); p->status=sloph_buffer_append(&b,part->text,part->length); end=part->end;
        if (!peek(p,"::")) break;
        take(p,"::");
        p->status=sloph_buffer_append(&b,"::",2u);
        if (!ident_token(&p->tokens[p->index])) error(p,"syntax.parse.unexpected_token","expected identifier",span_at(p->tokens[p->index].start,p->tokens[p->index].end));
    }
    if (out_span!=NULL) *out_span=span_at(start,end);
    { char *result=NULL; if (p->status==SLOPH_STATUS_OK) p->status=sloph_syntax_string(p->module,(const char *)b.data,b.length,&result); sloph_buffer_destroy(&b); return result; }
}
static SlophStatus lex(Parser *p) {
    Vector out={0}; size_t i=0u,n=p->source_length; const char *s=(const char *)p->source; const SlophLimits *l=sloph_context_limits(p->context);
    while (i<n && p->status==SLOPH_STATUS_OK) {
        size_t start; Token t; unsigned char c=(unsigned char)s[i];
        if (c=='\r') { if (i+1u>=n || s[i+1u]!='\n') { error(p,"syntax.parse.bare_carriage_return","bare carriage return is not allowed",span_at(i,i+1u)); break; } i+=2u; continue; }
        if (c==' '||c=='\t'||c=='\n') { i++; continue; }
        if (p->module->version==1u && i+1u<n && s[i]=='/' && s[i+1u]=='/') { i+=2u; while(i<n&&s[i]!='\n')i++; continue; }
        if (p->module->version==1u && i+1u<n && s[i]=='/' && s[i+1u]=='*') { size_t depth=1u; start=i; i+=2u; while(i<n&&depth){ if(i+1u<n&&s[i]=='/'&&s[i+1u]=='*'){depth++;i+=2u;}else if(i+1u<n&&s[i]=='*'&&s[i+1u]=='/'){depth--;i+=2u;}else i++; } if(depth){error(p,"syntax.parse.unterminated_comment","unterminated block comment",span_at(start,n));break;} continue; }
        start=i;
        if (i+1u<n && ((s[i]==':'&&s[i+1u]==':')||(s[i]=='-'&&s[i+1u]=='>')||(s[i]=='='&&s[i+1u]=='>')||(p->module->version==1u&&s[i]=='='&&s[i+1u]=='='))) i+=2u;
        else if (p->module->version==1u && c=='\"') { i++; while(i<n&&s[i]!='\"'){ if(s[i]=='\r'||s[i]=='\n'){error(p,"syntax.parse.byte_literal","newline in byte literal",span_at(start,i+1u));break;} if(s[i]=='\\')i++; i++; } if(p->status!=SLOPH_STATUS_OK)break; if(i>=n){error(p,"syntax.parse.byte_literal","unterminated byte literal",span_at(start,n));break;} i++; }
        else if (strchr("{}[](),;:=|",c)!=NULL || (p->module->version==1u&&strchr("+-*<?",c)!=NULL)) i++;
        else if (c=='-'&&i+1u<n&&isdigit((unsigned char)s[i+1u])) { i+=2u; while(i<n&&isdigit((unsigned char)s[i]))i++; }
        else if (isdigit(c)) { i++; while(i<n&&isdigit((unsigned char)s[i]))i++; }
        else if (isalpha(c)||c=='_') { i++; while(i<n&&(isalnum((unsigned char)s[i])||s[i]=='_'||s[i]=='.'))i++; }
        else { error(p,"syntax.parse.invalid_character","invalid source character",span_at(i,i+1u)); break; }
        if(i-start>l->token_bytes){limit(p,"token_bytes",l->token_bytes,span_at(start,i));break;}
        t.text=s+start;t.length=i-start;t.start=start;t.end=i; vector_push(p,&out,&t,sizeof(t));
        if(out.count>l->tokens){limit(p,"tokens",l->tokens,span_at(start,i));break;}
    }
    if(p->status==SLOPH_STATUS_OK){Token eof={"<eof>",5u,n,n};vector_push(p,&out,&eof,sizeof(eof));}
    p->token_count=out.count;p->tokens=vector_finish(p,&out,alignof(Token));return p->status;
}
static void count_node(Parser *p, SlophSpan span) { p->module->node_count++; if(p->module->node_count>sloph_context_limits(p->context)->ast_nodes)limit(p,"ast_nodes",sloph_context_limits(p->context)->ast_nodes,span); }
static void *node(Parser *p,size_t size,size_t alignment,SlophSpan span){void *x=NULL;if(p->status==SLOPH_STATUS_OK){count_node(p,span);if(p->status==SLOPH_STATUS_OK)p->status=sloph_syntax_alloc(p->module,size,alignment,&x);}return x;}

static SlophSyntaxType *parse_type(Parser *p);
static SlophSyntaxExpr *parse_expr(Parser *p);
static SlophSyntaxBlock *parse_block(Parser *p, SlophSyntaxType *propagation);
static SlophSyntaxBlock *synthetic_block(Parser *p, SlophSyntaxExpr *result,
                                         SlophSpan span);
static SlophSyntaxPattern *parse_target_pattern(Parser *p);
static SlophSyntaxBlock *parse_function_clauses(Parser *p, SlophSyntaxBinder *parameter, SlophSyntaxType *result_type);

static SlophSyntaxType *parse_type(Parser *p) {
    SlophSyntaxType *t; SlophSpan sp; char *name;
    if(peek(p,"fn")){Vector params={0},modes={0};size_t start=take(p,"fn")->start,i;take(p,"(");if(peek(p,")")){SlophSyntaxType *unit=node(p,sizeof(*unit),alignof(SlophSyntaxType),span_at(start,p->tokens[p->index].end));if(unit){unit->kind=SLOPH_SYNTAX_TYPE_NAMED;unit->span=span_at(start,p->tokens[p->index].end);sloph_syntax_string(p->module,"Unit",4u,&unit->as.name);}vector_push(p,&params,&unit,sizeof(unit));{char *own="own";vector_push(p,&modes,&own,sizeof(own));}}else while(p->status==SLOPH_STATUS_OK){char *mode="own";SlophSyntaxType *param;if(p->module->version==1u&&(peek(p,"own")||peek(p,"borrow"))){if(peek(p,"borrow")&&token_is(&p->tokens[p->index+1u],"mut")){take(p,"borrow");take(p,"mut");mode="borrow-mut";}else mode=copy_token(p,take(p,NULL));}param=parse_type(p);vector_push(p,&params,&param,sizeof(param));vector_push(p,&modes,&mode,sizeof(mode));if(!peek(p,","))break;take(p,",");}take(p,")");take(p,"->");t=parse_type(p);for(i=params.count;i>0u;i--){SlophSyntaxType *f=node(p,sizeof(*f),alignof(SlophSyntaxType),span_at(start,t?t->span.end:start));if(f){f->kind=SLOPH_SYNTAX_TYPE_FUNCTION;f->span=span_at(start,t->span.end);f->as.function.parameter=((SlophSyntaxType**)params.data)[i-1u];f->as.function.result=t;f->as.function.mode=((char**)modes.data)[i-1u];}t=f;}vector_destroy(p,&params);vector_destroy(p,&modes);return t;}
    name=parse_path(p,&sp); t=node(p,sizeof(*t),alignof(SlophSyntaxType),sp); if(!t)return NULL;t->span=sp;
    if(name!=NULL&&strcmp(name,"Int")==0){t->kind=SLOPH_SYNTAX_TYPE_INT;return t;}
    if(peek(p,"[")){Vector args={0};take(p,"[");while(!peek(p,"]")&&p->status==SLOPH_STATUS_OK){SlophSyntaxType *a=parse_type(p);vector_push(p,&args,&a,sizeof(a));if(!peek(p,","))break;take(p,",");}take(p,"]");t->kind=SLOPH_SYNTAX_TYPE_APPLIED;t->as.applied.constructor=name;t->as.applied.count=args.count;t->as.applied.items=vector_finish(p,&args,alignof(SlophSyntaxType*));t->span.end=p->tokens[p->index-1u].end;return t;}
    t->kind=SLOPH_SYNTAX_TYPE_NAMED;t->as.name=name;return t;
}
static SlophSyntaxBinder parse_binder(Parser *p,bool inferred,bool mode_allowed){SlophSyntaxBinder b;Token *n;memset(&b,0,sizeof(b));n=ident(p,false,"binder");b.name=copy_token(p,n);b.span.start=n->start;b.mode="own";if(inferred&&!peek(p,":")){b.type=node(p,sizeof(*b.type),alignof(SlophSyntaxType),span_at(n->end,n->end));if(b.type){b.type->kind=SLOPH_SYNTAX_TYPE_INFERRED;b.type->span=span_at(n->end,n->end);}}else{take(p,":");if(mode_allowed&&p->module->version==1u&&(peek(p,"own")||peek(p,"borrow"))){if(peek(p,"borrow")&&token_is(&p->tokens[p->index+1u],"mut")){take(p,"borrow");take(p,"mut");b.mode="borrow-mut";}else b.mode=copy_token(p,take(p,NULL));}b.type=parse_type(p);}b.span.end=b.type?b.type->span.end:n->end;count_node(p,b.span);return b;}
static SlophSyntaxExpr *new_expr(Parser *p,SlophSyntaxExprKind k,SlophSpan s){SlophSyntaxExpr *e=node(p,sizeof(*e),alignof(SlophSyntaxExpr),s);if(e){e->kind=k;e->span=s;}return e;}
static SlophSyntaxExpr *parse_atom(Parser *p);
static int precedence(Token *t){if(token_is(t,"==")||token_is(t,"<"))return 3;if(token_is(t,"+")||token_is(t,"-"))return 6;if(token_is(t,"*"))return 7;return -1;}
static SlophSyntaxExpr *parse_binary(Parser *p,int minimum){SlophSyntaxExpr *left;const SlophLimits*l=sloph_context_limits(p->context);p->depth++;if(p->depth>l->syntax_depth){limit(p,"syntax_depth",l->syntax_depth,span_at(p->tokens[p->index].start,p->tokens[p->index].end));p->depth--;return NULL;}left=parse_atom(p);while(p->status==SLOPH_STATUS_OK&&(peek(p,"(")||peek(p,"["))){Vector types={0},args={0};size_t start=left->span.start,end;if(peek(p,"[")){take(p,"[");while(!peek(p,"]")&&p->status==SLOPH_STATUS_OK){SlophSyntaxType*t=parse_type(p);vector_push(p,&types,&t,sizeof(t));if(!peek(p,","))break;take(p,",");}take(p,"]");}take(p,"(");while(!peek(p,")")&&p->status==SLOPH_STATUS_OK){SlophSyntaxExpr*a=parse_expr(p);vector_push(p,&args,&a,sizeof(a));if(!peek(p,","))break;take(p,",");}end=take(p,")")->end;{SlophSyntaxExpr*call=new_expr(p,SLOPH_SYNTAX_EXPR_CALL,span_at(start,end));if(call){call->as.call.function=left;call->as.call.argument_count=args.count;call->as.call.arguments=vector_finish(p,&args,alignof(SlophSyntaxExpr*));call->as.call.type_argument_count=types.count;call->as.call.type_arguments=vector_finish(p,&types,alignof(SlophSyntaxType*));}left=call;}}
    if(p->module->version==1u){
        while(p->status==SLOPH_STATUS_OK&&precedence(&p->tokens[p->index])>=minimum){
            Token*op=take(p,NULL);
            SlophSyntaxExpr*right=parse_binary(p,precedence(op)+1);
            SlophSyntaxExpr*e=new_expr(p,SLOPH_SYNTAX_EXPR_BINARY,span_at(left->span.start,right?right->span.end:op->end));
            if(e){e->as.binary.operator_=copy_token(p,op);e->as.binary.left=left;e->as.binary.right=right;}
            left=e;
        }
    }
    p->depth--;
    return left;
}
static SlophSyntaxExpr *parse_expr(Parser *p){return parse_binary(p,0);}
static unsigned char hex_value(char c){return (unsigned char)(c>='0'&&c<='9'?c-'0':(c>='a'&&c<='f'?c-'a'+10:c-'A'+10));}
static bool valid_utf8(const unsigned char *data,size_t length){size_t i=0u;while(i<length){unsigned char c=data[i++];size_t extra;unsigned value,minimum;if(c<0x80u)continue;if(c>=0xc2u&&c<=0xdfu){extra=1u;value=c&0x1fu;minimum=0x80u;}else if(c>=0xe0u&&c<=0xefu){extra=2u;value=c&0x0fu;minimum=0x800u;}else if(c>=0xf0u&&c<=0xf4u){extra=3u;value=c&0x07u;minimum=0x10000u;}else return false;if(extra>length-i)return false;while(extra--){unsigned char q=data[i++];if((q&0xc0u)!=0x80u)return false;value=(value<<6u)|(q&0x3fu);}if(value<minimum||value>0x10ffffu||(value>=0xd800u&&value<=0xdfffu))return false;}return true;}
static SlophSyntaxExpr *parse_atom(Parser *p){Token*t=&p->tokens[p->index];size_t start=t->start;SlophSyntaxExpr*e;SlophSpan sp;
    if(p->module->version==1u&&(peek(p,"if")||peek(p,"unless"))){bool unless=peek(p,"unless");take(p,NULL);{SlophSyntaxExpr*c=parse_expr(p);SlophSyntaxBlock*a=parse_block(p,NULL);take(p,"else");{SlophSyntaxBlock*b=parse_block(p,NULL);e=new_expr(p,SLOPH_SYNTAX_EXPR_IF,span_at(start,b?b->span.end:start));if(e){e->as.if_.condition=c;e->as.if_.then_body=unless?b:a;e->as.if_.else_body=unless?a:b;}return e;}}}
    if(p->module->version==1u&&peek(p,"fn")){Vector bs={0};take(p,"fn");take(p,"(");while(!peek(p,")")&&p->status==SLOPH_STATUS_OK){SlophSyntaxBinder b=parse_binder(p,false,true);vector_push(p,&bs,&b,sizeof(b));if(!peek(p,","))break;take(p,",");}take(p,")");take(p,"->");{SlophSyntaxType*r=parse_type(p);SlophSyntaxBlock*b=parse_block(p,NULL);e=new_expr(p,SLOPH_SYNTAX_EXPR_LAMBDA,span_at(start,b?b->span.end:start));if(e){e->as.lambda.parameter_count=bs.count;e->as.lambda.parameters=vector_finish(p,&bs,alignof(SlophSyntaxBinder));e->as.lambda.result_type=r;e->as.lambda.body=b;}return e;}}
    if(p->module->version==1u&&(peek(p,"bytes")||peek(p,"ascii")||peek(p,"utf8"))&&p->index+1u<p->token_count&&p->tokens[p->index+1u].length>=2u&&p->tokens[p->index+1u].text[0]=='\"'&&t->end==p->tokens[p->index+1u].start){SlophSyntaxBytesKind kind=peek(p,"bytes")?SLOPH_SYNTAX_BYTES_RAW:(peek(p,"ascii")?SLOPH_SYNTAX_BYTES_ASCII:SLOPH_SYNTAX_BYTES_UTF8);Vector bytes={0};Token*literal;size_t i;take(p,NULL);literal=take(p,NULL);i=1u;while(i+1u<literal->length){unsigned char c=(unsigned char)literal->text[i++];if(c=='\\'){char q=literal->text[i++];if(q=='x'&&i+1u<literal->length){if(!isxdigit((unsigned char)literal->text[i])||!isxdigit((unsigned char)literal->text[i+1u])){error(p,"syntax.parse.unexpected_token","unknown literal escape",span_at(literal->start,literal->end));break;}c=(unsigned char)((hex_value(literal->text[i])<<4)|hex_value(literal->text[i+1u]));i+=2u;}else{switch(q){case'n':c=10;break;case'r':c=13;break;case't':c=9;break;case'0':c=0;break;case'\\':c=92;break;case'\"':c=34;break;default:error(p,"syntax.parse.unexpected_token","unknown literal escape",span_at(literal->start,literal->end));}}}if(kind==SLOPH_SYNTAX_BYTES_ASCII&&c>0x7fu){error(p,"syntax.parse.ascii_literal","ASCII literal contains a non-ASCII byte",span_at(literal->start,literal->end));break;}vector_push(p,&bytes,&c,1u);}if(p->status==SLOPH_STATUS_OK&&kind==SLOPH_SYNTAX_BYTES_UTF8&&!valid_utf8(bytes.data,bytes.count))error(p,"syntax.parse.utf8_literal","UTF-8 literal contains an invalid byte sequence",span_at(literal->start,literal->end));e=new_expr(p,SLOPH_SYNTAX_EXPR_BYTES,span_at(start,literal->end));if(e){e->as.bytes.kind=kind;e->as.bytes.length=bytes.count;e->as.bytes.data=vector_finish(p,&bytes,alignof(unsigned char));}else vector_destroy(p,&bytes);return e;}
    if(p->module->version==1u&&t->length>=2u&&t->text[0]=='\"'){error(p,"syntax.parse.literal_prefix","string-like literals require an explicit utf8, ascii, or bytes prefix",span_at(t->start,t->end));return NULL;}
    if((t->length&&isdigit((unsigned char)t->text[0]))||(t->length>1u&&t->text[0]=='-'&&isdigit((unsigned char)t->text[1]))||(p->module->version==1u&&peek(p,"-")&&isdigit((unsigned char)p->tokens[p->index+1u].text[0]))){SlophBuffer b;Token*num=t;sloph_buffer_init(&b,p->context,sloph_context_limits(p->context)->literal_digits+2u);if(peek(p,"-")){take(p,"-");num=take(p,NULL);sloph_buffer_append_byte(&b,'-');}else take(p,NULL);if(num->length-(num->text[0]=='-')>sloph_context_limits(p->context)->literal_digits)limit(p,"literal_digits",sloph_context_limits(p->context)->literal_digits,span_at(start,num->end));sloph_buffer_append(&b,num->text,num->length);e=new_expr(p,SLOPH_SYNTAX_EXPR_INT,span_at(start,num->end));if(e)sloph_syntax_string(p->module,(char*)b.data,b.length,&e->as.integer);sloph_buffer_destroy(&b);return e;}
    if(p->module->version==0u&&peek(p,"primitive")){Vector args={0};char*name;take(p,"primitive");name=copy_token(p,take(p,NULL));take(p,"(");while(!peek(p,")")&&p->status==SLOPH_STATUS_OK){SlophSyntaxExpr*a=parse_expr(p);vector_push(p,&args,&a,sizeof(a));if(!peek(p,","))break;take(p,",");}sp.end=take(p,")")->end;e=new_expr(p,SLOPH_SYNTAX_EXPR_PRIMITIVE,span_at(start,sp.end));if(e){e->as.primitive.name=name;e->as.primitive.argument_count=args.count;e->as.primitive.arguments=vector_finish(p,&args,alignof(SlophSyntaxExpr*));}return e;}
    if(peek(p,"case")){Vector alts={0};take(p,"case");{SlophSyntaxExpr*scr=parse_expr(p);take(p,"->");{SlophSyntaxType*rt=parse_type(p);take(p,"{");while(!peek(p,"}")&&p->status==SLOPH_STATUS_OK){SlophSyntaxCaseAlternative a;Vector bs={0};memset(&a,0,sizeof(a));a.span.start=p->tokens[p->index].start;a.constructor=parse_path(p,NULL);take(p,"(");while(!peek(p,")")&&p->status==SLOPH_STATUS_OK){SlophSyntaxBinder b=parse_binder(p,true,false);vector_push(p,&bs,&b,sizeof(b));if(!peek(p,","))break;take(p,",");}take(p,")");take(p,"=>");a.body=parse_block(p,NULL);a.span.end=a.body?a.body->span.end:a.span.start;if(peek(p,";"))a.span.end=take(p,";")->end;a.binder_count=bs.count;a.binders=vector_finish(p,&bs,alignof(SlophSyntaxBinder));count_node(p,a.span);vector_push(p,&alts,&a,sizeof(a));}sp.end=take(p,"}")->end;e=new_expr(p,SLOPH_SYNTAX_EXPR_CASE,span_at(start,sp.end));if(e){e->as.case_.scrutinee=scr;e->as.case_.result_type=rt;e->as.case_.alternative_count=alts.count;e->as.case_.alternatives=vector_finish(p,&alts,alignof(SlophSyntaxCaseAlternative));}return e;}}}
    if(peek(p,"(")){take(p,"(");e=parse_expr(p);take(p,")");return e;}
    {char*name=parse_path(p,&sp);Vector types={0},args={0};if(peek(p,"[")){take(p,"[");while(!peek(p,"]")&&p->status==SLOPH_STATUS_OK){SlophSyntaxType*x=parse_type(p);vector_push(p,&types,&x,sizeof(x));if(!peek(p,","))break;take(p,",");}take(p,"]");}if(peek(p,"(")){take(p,"(");while(!peek(p,")")&&p->status==SLOPH_STATUS_OK){SlophSyntaxExpr*a=parse_expr(p);vector_push(p,&args,&a,sizeof(a));if(!peek(p,","))break;take(p,",");}sp.end=take(p,")")->end;if(name&&strrchr(name,':')!=NULL&&isupper((unsigned char)strrchr(name,':')[1])){e=new_expr(p,SLOPH_SYNTAX_EXPR_CONSTRUCTOR,sp);if(e){e->as.constructor.constructor=name;e->as.constructor.argument_count=args.count;e->as.constructor.arguments=vector_finish(p,&args,alignof(SlophSyntaxExpr*));e->as.constructor.type_argument_count=types.count;e->as.constructor.type_arguments=vector_finish(p,&types,alignof(SlophSyntaxType*));}}else{SlophSyntaxExpr*fn=new_expr(p,strstr(name,"::")?SLOPH_SYNTAX_EXPR_GLOBAL:SLOPH_SYNTAX_EXPR_LOCAL,span_at(start,sp.start+(name?strlen(name):0u)));if(fn)fn->as.name=name;e=new_expr(p,SLOPH_SYNTAX_EXPR_CALL,sp);if(e){e->as.call.function=fn;e->as.call.argument_count=args.count;e->as.call.arguments=vector_finish(p,&args,alignof(SlophSyntaxExpr*));e->as.call.type_argument_count=types.count;e->as.call.type_arguments=vector_finish(p,&types,alignof(SlophSyntaxType*));}}return e;}vector_destroy(p,&types);e=new_expr(p,strstr(name,"::")?SLOPH_SYNTAX_EXPR_GLOBAL:SLOPH_SYNTAX_EXPR_LOCAL,sp);if(e)e->as.name=name;return e;}
}

static SlophSyntaxBlock *lower_propagation(Parser *p, SlophSyntaxBlock *block,
                                           SlophSyntaxType *result_type) {
    size_t i, rest_count;
    for (i = 0u; i < block->statement_count; ++i)
        if (block->statements[i].propagation) break;
    if (i == block->statement_count) return block;
    rest_count = block->statement_count - i - 1u;
    {
        SlophSyntaxStatement *statement = &block->statements[i];
        SlophSyntaxBlock *rest = node(p, sizeof(*rest), alignof(SlophSyntaxBlock),
                                      span_at(statement->span.end, block->span.end));
        SlophSyntaxBinder success = statement->as.let.binder;
        SlophSyntaxBinder failure;
        SlophSyntaxExpr *failure_local, *error_value, *case_expr;
        SlophSyntaxBlock *error_body;
        SlophSyntaxCaseAlternative *alternatives;
        memset(&failure, 0, sizeof(failure));
        if (rest_count != 0u) {
            p->status = sloph_syntax_alloc(p->module,
                rest_count * sizeof(*rest->statements), alignof(SlophSyntaxStatement),
                (void **)&rest->statements);
            if (p->status == SLOPH_STATUS_OK)
                memcpy(rest->statements, block->statements + i + 1u,
                       rest_count * sizeof(*rest->statements));
        }
        rest->statement_count = rest_count;
        rest->result = block->result;
        rest->span = span_at(statement->span.end, block->span.end);
        rest = lower_propagation(p, rest, result_type);
        failure.span = statement->span;
        {
            size_t name_length = strlen(success.name);
            SlophBuffer name;
            sloph_buffer_init(&name, p->context,
                              sloph_context_limits(p->context)->token_bytes);
            sloph_buffer_append(&name, success.name, name_length);
            sloph_buffer_append(&name, "_failure", 8u);
            sloph_syntax_string(p->module, (char *)name.data, name.length,
                                &failure.name);
            sloph_buffer_destroy(&name);
        }
        failure.mode = "own";
        failure.type = node(p, sizeof(*failure.type), alignof(SlophSyntaxType),
                            statement->span);
        failure.type->kind = SLOPH_SYNTAX_TYPE_INFERRED;
        failure.type->span = statement->span;
        failure_local = new_expr(p, SLOPH_SYNTAX_EXPR_LOCAL, statement->span);
        failure_local->as.name = failure.name;
        error_value = new_expr(p, SLOPH_SYNTAX_EXPR_CONSTRUCTOR, statement->span);
        error_value->as.constructor.constructor = "Result::Err";
        error_value->as.constructor.argument_count = 1u;
        p->status = sloph_syntax_alloc(p->module, sizeof(SlophSyntaxExpr *),
                                       alignof(SlophSyntaxExpr *),
                                       (void **)&error_value->as.constructor.arguments);
        error_value->as.constructor.arguments[0] = failure_local;
        error_value->as.constructor.type_arguments = result_type->as.applied.items;
        error_value->as.constructor.type_argument_count = result_type->as.applied.count;
        error_body = synthetic_block(p, error_value, statement->span);
        p->status = sloph_syntax_alloc(p->module,
            2u * sizeof(*alternatives), alignof(SlophSyntaxCaseAlternative),
            (void **)&alternatives);
        memset(alternatives, 0, 2u * sizeof(*alternatives));
        alternatives[0].constructor = "Result::Ok";
        alternatives[0].binder_count = 1u;
        p->status = sloph_syntax_alloc(p->module, sizeof(success),
                                       alignof(SlophSyntaxBinder),
                                       (void **)&alternatives[0].binders);
        alternatives[0].binders[0] = success;
        alternatives[0].body = rest;
        alternatives[0].span = statement->span;
        alternatives[1].constructor = "Result::Err";
        alternatives[1].binder_count = 1u;
        p->status = sloph_syntax_alloc(p->module, sizeof(failure),
                                       alignof(SlophSyntaxBinder),
                                       (void **)&alternatives[1].binders);
        alternatives[1].binders[0] = failure;
        alternatives[1].body = error_body;
        alternatives[1].span = statement->span;
        case_expr = new_expr(p, SLOPH_SYNTAX_EXPR_CASE,
                             span_at(statement->span.start, block->span.end));
        case_expr->as.case_.scrutinee = statement->as.let.value;
        case_expr->as.case_.result_type = result_type;
        case_expr->as.case_.alternatives = alternatives;
        case_expr->as.case_.alternative_count = 2u;
        block->statement_count = i;
        block->result = case_expr;
        return block;
    }
}

static SlophSyntaxBlock *parse_block(Parser*p,SlophSyntaxType*propagation){Vector statements={0};size_t start=take(p,"{")->start,end;SlophSyntaxBlock*b;while(p->status==SLOPH_STATUS_OK&&(peek(p,"let")||(p->module->version==1u&&peek(p,"defer")))){SlophSyntaxStatement s;memset(&s,0,sizeof(s));s.span.start=p->tokens[p->index].start;if(peek(p,"defer")){take(p,"defer");s.kind=SLOPH_SYNTAX_STMT_DEFER;s.as.defer_call=parse_expr(p);s.span.end=take(p,";")->end;}else{take(p,"let");s.kind=SLOPH_SYNTAX_STMT_LET;s.as.let.binder=parse_binder(p,p->module->version==1u,false);take(p,"=");s.as.let.value=parse_expr(p);if(peek(p,"?")){Token *question=take(p,"?");s.propagation=true;const char *constructor=(propagation!=NULL&&propagation->kind==SLOPH_SYNTAX_TYPE_APPLIED)?propagation->as.applied.constructor:NULL;const char *tail=constructor!=NULL?strrchr(constructor,':'):NULL;if(constructor==NULL||propagation->as.applied.count!=2u||strcmp(tail!=NULL?tail+1u:constructor,"Result")!=0)error(p,"syntax.parse.propagation_context","the '?' operator requires a top-level let binding in a function returning Result[Success, Failure]",span_at(question->start,question->end));}s.span.end=take(p,";")->end;}count_node(p,s.span);vector_push(p,&statements,&s,sizeof(s));}b=node(p,sizeof(*b),alignof(SlophSyntaxBlock),span_at(start,start));if(!b){vector_destroy(p,&statements);return NULL;}b->statement_count=statements.count;b->statements=vector_finish(p,&statements,alignof(SlophSyntaxStatement));b->result=parse_expr(p);if(peek(p,"?")){Token *question=take(p,"?");error(p,"syntax.parse.propagation_context","the '?' operator is only supported on let bindings",span_at(question->start,question->end));}if(peek(p,";"))take(p,";");end=take(p,"}")->end;b->span=span_at(start,end);return propagation!=NULL?lower_propagation(p,b,propagation):b;}


typedef struct SyntaxClause {
    bool integer_pattern;
    char *pattern;
    SlophSyntaxExpr *guard;
    SlophSyntaxBlock *body;
    SlophSpan span;
} SyntaxClause;

static SlophSyntaxBlock *synthetic_block(Parser *p, SlophSyntaxExpr *result,
                                         SlophSpan span) {
    SlophSyntaxBlock *block = node(p, sizeof(*block), alignof(SlophSyntaxBlock), span);
    if (block != NULL) { block->result = result; block->span = span; }
    return block;
}

static SlophSyntaxBlock *clause_alias_block(Parser *p,
                                            SlophSyntaxBlock *body,
                                            const char *alias,
                                            const SlophSyntaxBinder *parameter,
                                            SlophSpan span) {
    SlophSyntaxStatement *statements;
    SlophSyntaxExpr *source;
    if (body == NULL || alias == NULL || strcmp(alias, "_") == 0 ||
        strcmp(alias, parameter->name) == 0) return body;
    if (sloph_syntax_alloc(p->module,
            (body->statement_count + 1u) * sizeof(*statements),
            alignof(SlophSyntaxStatement), (void **)&statements) !=
        SLOPH_STATUS_OK) return NULL;
    memset(&statements[0], 0, sizeof(statements[0]));
    statements[0].kind = SLOPH_SYNTAX_STMT_LET;
    statements[0].span = span;
    statements[0].as.let.binder.name = (char *)alias;
    statements[0].as.let.binder.type = parameter->type;
    statements[0].as.let.binder.mode = "own";
    statements[0].as.let.binder.span = span;
    source = new_expr(p, SLOPH_SYNTAX_EXPR_LOCAL, span);
    if (source == NULL) return NULL;
    source->as.name = parameter->name;
    statements[0].as.let.value = source;
    if (body->statement_count != 0u)
        memcpy(statements + 1u, body->statements,
               body->statement_count * sizeof(*statements));
    body->statements = statements;
    ++body->statement_count;
    body->span = span;
    count_node(p, span);
    return body;
}

static SlophSyntaxBlock *parse_function_clauses(Parser *p,
                                                SlophSyntaxBinder *parameter,
                                                SlophSyntaxType *result_type) {
    Vector clauses = {0};
    bool has_catchall = false;
    SlophSyntaxBlock *fallback = NULL;
    size_t i;
    if (peek(p, "|") && p->tokens[p->index + 1u].length != 0u &&
        isupper((unsigned char)p->tokens[p->index + 1u].text[0])) {
        Vector alternatives = {0};
        size_t start = p->tokens[p->index].start;
        while (peek(p, "|") && p->status == SLOPH_STATUS_OK) {
            SlophSyntaxCaseAlternative alternative;
            Vector binders = {0};
            memset(&alternative, 0, sizeof(alternative));
            alternative.span.start = take(p, "|")->start;
            alternative.constructor = parse_path(p, NULL);
            take(p, "(");
            while (!peek(p, ")") && p->status == SLOPH_STATUS_OK) {
                SlophSyntaxBinder binder = parse_binder(p, true, false);
                vector_push(p, &binders, &binder, sizeof(binder));
                if (!peek(p, ",")) break;
                take(p, ",");
            }
            take(p, ")"); take(p, "=>");
            alternative.body = parse_block(p, NULL);
            alternative.span.end = alternative.body->span.end;
            alternative.binder_count = binders.count;
            alternative.binders = vector_finish(p, &binders, alignof(SlophSyntaxBinder));
            vector_push(p, &alternatives, &alternative, sizeof(alternative));
        }
        if (alternatives.count != 0u) {
            SlophSyntaxExpr *local = new_expr(p, SLOPH_SYNTAX_EXPR_LOCAL,
                                              span_at(start, ((SlophSyntaxCaseAlternative *)alternatives.data)[alternatives.count - 1u].span.end));
            SlophSyntaxExpr *case_expr = new_expr(p, SLOPH_SYNTAX_EXPR_CASE, local->span);
            local->as.name = parameter->name;
            case_expr->as.case_.scrutinee = local;
            case_expr->as.case_.result_type = result_type;
            case_expr->as.case_.alternative_count = alternatives.count;
            case_expr->as.case_.alternatives = vector_finish(p, &alternatives, alignof(SlophSyntaxCaseAlternative));
            return synthetic_block(p, case_expr, case_expr->span);
        }
        vector_destroy(p, &alternatives);
    }
    while (peek(p, "|") && p->status == SLOPH_STATUS_OK) {
        SyntaxClause clause;
        Token *pattern_token;
        memset(&clause, 0, sizeof(clause));
        clause.span.start = take(p, "|")->start;
        pattern_token = &p->tokens[p->index];
        clause.integer_pattern = pattern_token->length != 0u &&
            (isdigit((unsigned char)pattern_token->text[0]) ||
             (pattern_token->text[0] == '-' && pattern_token->length > 1u));
        if (clause.integer_pattern) {
            take(p, NULL);
            clause.pattern = copy_token(p, pattern_token);
        } else {
            clause.pattern = copy_token(p, ident(p, false, "pattern binder"));
        }
        if (peek(p, "when")) {
            Token *when_token = take(p, "when");
            if (clause.integer_pattern) {
                error_token(p,
                    "guards on integer function patterns are not supported",
                    when_token);
                vector_destroy(p, &clauses);
                return NULL;
            }
            clause.guard = parse_expr(p);
        } else if (!clause.integer_pattern) {
            has_catchall = true;
        }
        take(p, "=>");
        if (peek(p, "{")) clause.body = parse_block(p, NULL);
        else {
            SlophSyntaxExpr *value = parse_expr(p);
            clause.body = synthetic_block(p, value, value->span);
        }
        clause.span.end = clause.body != NULL ? clause.body->span.end : clause.span.start;
        vector_push(p, &clauses, &clause, sizeof(clause));
    }
    if (!has_catchall)
        error_token(p,
            "integer function clauses require a final unguarded binder or underscore catch-all",
            &p->tokens[p->index]);
    for (i = clauses.count; i > 0u && p->status == SLOPH_STATUS_OK; --i) {
        SyntaxClause *clause = &((SyntaxClause *)clauses.data)[i - 1u];
        if (!clause->integer_pattern && clause->guard == NULL) {
            fallback = clause_alias_block(p, clause->body, clause->pattern,
                                          parameter, clause->span);
        } else {
            SlophSyntaxExpr *condition;
            SlophSyntaxExpr *choice;
            if (clause->integer_pattern) {
                SlophSyntaxExpr *local = new_expr(p, SLOPH_SYNTAX_EXPR_LOCAL, clause->span);
                SlophSyntaxExpr *literal = new_expr(p, SLOPH_SYNTAX_EXPR_INT, clause->span);
                local->as.name = parameter->name;
                literal->as.integer = clause->pattern;
                condition = new_expr(p, SLOPH_SYNTAX_EXPR_BINARY, clause->span);
                condition->as.binary.operator_ = "==";
                condition->as.binary.left = local;
                condition->as.binary.right = literal;
            } else {
                condition = clause->guard;
            }
            if (fallback == NULL)
                fallback = synthetic_block(p, new_expr(p, SLOPH_SYNTAX_EXPR_LOCAL, clause->span), clause->span);
            choice = new_expr(p, SLOPH_SYNTAX_EXPR_IF, clause->span);
            choice->as.if_.condition = condition;
            choice->as.if_.then_body = clause->body;
            choice->as.if_.else_body = fallback;
            fallback = synthetic_block(p, choice, clause->span);
            if (!clause->integer_pattern)
                fallback = clause_alias_block(p, fallback, clause->pattern,
                                              parameter, clause->span);
        }
    }
    vector_destroy(p, &clauses);
    (void)result_type;
    return fallback;
}

static void parse_string_list(Parser*p,Vector*v,bool upper){take(p,"[");while(!peek(p,"]")&&p->status==SLOPH_STATUS_OK){char*s=copy_token(p,ident(p,upper,"name"));vector_push(p,v,&s,sizeof(s));if(!peek(p,","))break;take(p,",");}take(p,"]");}
static SlophSyntaxDirectImport parse_direct_import(Parser*p,bool public_){SlophSyntaxDirectImport d;SlophBuffer b;Vector names={0};Token*part;memset(&d,0,sizeof(d));d.span.start=p->tokens[p->index].start;d.public_=public_;sloph_buffer_init(&b,p->context,sloph_context_limits(p->context)->token_bytes*16u);part=ident(p,false,"module component");sloph_buffer_append(&b,part->text,part->length);while(peek(p,"::")){take(p,"::");if(peek(p,"{"))break;part=ident(p,false,"module component");sloph_buffer_append(&b,"::",2u);sloph_buffer_append(&b,part->text,part->length);}sloph_syntax_string(p->module,(char*)b.data,b.length,&d.module);sloph_buffer_destroy(&b);take(p,"{");while(!peek(p,"}")&&p->status==SLOPH_STATUS_OK){Token*n=&p->tokens[p->index];char*s;if(!ident_token(n)){error(p,"syntax.parse.unexpected_token","expected import name",span_at(n->start,n->end));break;}p->index++;s=copy_token(p,n);vector_push(p,&names,&s,sizeof(s));if(!peek(p,","))break;take(p,",");}d.span.end=take(p,"}")->end;d.name_count=names.count;d.names=vector_finish(p,&names,alignof(char*));count_node(p,d.span);return d;}

static SlophSyntaxPattern *parse_target_pattern(Parser *p) {
    size_t start = p->tokens[p->index].start;
    SlophSyntaxPattern *pattern = node(p, sizeof(*pattern), alignof(SlophSyntaxPattern), span_at(start, start));
    if (pattern == NULL) return NULL;
    if (peek(p, "(")) {
        Vector items = {0};
        take(p, "(");
        pattern->kind = SLOPH_SYNTAX_PATTERN_TUPLE;
        while (!peek(p, ")") && p->status == SLOPH_STATUS_OK) {
            SlophSyntaxPattern *item = parse_target_pattern(p);
            vector_push(p, &items, &item, sizeof(item));
            if (!peek(p, ",")) break;
            take(p, ",");
        }
        pattern->as.tuple.count = items.count;
        pattern->as.tuple.items = vector_finish(p, &items, alignof(SlophSyntaxPattern *));
        pattern->span = span_at(start, take(p, ")")->end);
    } else {
        pattern->kind = SLOPH_SYNTAX_PATTERN_CONSTANT;
        pattern->as.constant = parse_path(p, &pattern->span);
    }
    return pattern;
}

static SlophSyntaxImport parse_import(Parser*p,bool public_){SlophSyntaxImport x;memset(&x,0,sizeof(x));x.span.start=take(p,"import")->start;if(p->module->version==1u&&peek(p,"case")){/* conditional shape is preserved; target parser is intentionally compact */Vector alts={0};x.kind=SLOPH_SYNTAX_IMPORT_CONDITIONAL;take(p,"case");x.as.conditional.selector=parse_path(p,NULL);take(p,"{");while(!peek(p,"}")&&p->status==SLOPH_STATUS_OK){SlophSyntaxConditionalAlternative a;SlophSyntaxPattern*pat;memset(&a,0,sizeof(a));a.span.start=p->tokens[p->index].start;pat=parse_target_pattern(p);a.pattern=pat;take(p,"=>");a.import_=parse_direct_import(p,false);a.span.end=take(p,";")->end;vector_push(p,&alts,&a,sizeof(a));}x.span.end=take(p,"}")->end;x.as.conditional.alternative_count=alts.count;x.as.conditional.alternatives=vector_finish(p,&alts,alignof(SlophSyntaxConditionalAlternative));}else{x.kind=SLOPH_SYNTAX_IMPORT_DIRECT;x.as.direct=parse_direct_import(p,public_);x.span.end=take(p,";")->end;x.as.direct.span=span_at(x.span.start,x.span.end);}count_node(p,x.span);return x;}
static void parse_module(Parser*p){SlophSyntaxModule*m=p->module;Vector imports={0},types={0},functions={0},values={0};SlophBuffer name;Token*part;m->span.start=take(p,"module")->start;sloph_buffer_init(&name,p->context,sloph_context_limits(p->context)->token_bytes*16u);part=ident(p,false,"module component");sloph_buffer_append(&name,part->text,part->length);while(peek(p,"::")){take(p,"::");part=ident(p,false,"module component");sloph_buffer_append(&name,"::",2u);sloph_buffer_append(&name,part->text,part->length);}sloph_syntax_string(m,(char*)name.data,name.length,&m->name);sloph_buffer_destroy(&name);if(p->module->version==1u&&peek(p,"when")){size_t availability_start=take(p,"when")->start;SlophSyntaxAvailability *availability=node(p,sizeof(*availability),alignof(SlophSyntaxAvailability),span_at(availability_start,availability_start));availability->selector=parse_path(p,NULL);take(p,"is");availability->pattern=parse_target_pattern(p);availability->span=span_at(availability_start,availability->pattern?availability->pattern->span.end:availability_start);m->availability=availability;}take(p,";");while(peek(p,"import")||(p->module->version==1u&&peek(p,"public")&&token_is(&p->tokens[p->index+1u],"import"))){bool pub=false;SlophSyntaxImport x;if(peek(p,"public")){take(p,"public");pub=true;}x=parse_import(p,pub);vector_push(p,&imports,&x,sizeof(x));}
    while(!peek(p,"<eof>")&&p->status==SLOPH_STATUS_OK){bool pub=false,owned_intrinsic=false;size_t start=p->tokens[p->index].start;if(peek(p,"public")){pub=true;take(p,"public");}if(p->module->version==1u&&peek(p,"owned")&&token_is(&p->tokens[p->index+1u],"intrinsic")){owned_intrinsic=true;take(p,"owned");}if(p->module->version==1u&&(peek(p,"intrinsic")||peek(p,"foreign"))){bool foreign=peek(p,"foreign");take(p,NULL);if(peek(p,"type")){SlophSyntaxTypeDecl d;memset(&d,0,sizeof(d));take(p,"type");d.kind=SLOPH_SYNTAX_TYPE_DECL_INTRINSIC;d.name=copy_token(p,ident(p,true,"type"));d.public_=pub;d.owned=owned_intrinsic;d.span=span_at(start,take(p,";")->end);vector_push(p,&types,&d,sizeof(d));}else{SlophSyntaxFunction f;Vector bs={0};memset(&f,0,sizeof(f));take(p,"fn");f.kind=foreign?SLOPH_SYNTAX_FUNCTION_FOREIGN:SLOPH_SYNTAX_FUNCTION_INTRINSIC;f.name=copy_token(p,ident(p,false,"function"));take(p,"(");while(!peek(p,")")){SlophSyntaxBinder b=parse_binder(p,false,true);vector_push(p,&bs,&b,sizeof(b));if(!peek(p,","))break;take(p,",");}take(p,")");take(p,"->");f.result_type=parse_type(p);take(p,"=");f.binding=copy_token(p,take(p,NULL));f.span=span_at(start,take(p,";")->end);f.public_=pub;f.parameter_count=bs.count;f.parameters=vector_finish(p,&bs,alignof(SlophSyntaxBinder));vector_push(p,&functions,&f,sizeof(f));}}
        else if(peek(p,"owned")||peek(p,"type")){SlophSyntaxTypeDecl d;Vector ps={0},ctors={0};memset(&d,0,sizeof(d));d.kind=SLOPH_SYNTAX_TYPE_DECL_ENUM;d.public_=pub;if(peek(p,"owned")){d.owned=true;take(p,"owned");}take(p,"type");d.name=copy_token(p,ident(p,true,"type"));if(peek(p,"["))parse_string_list(p,&ps,true);take(p,"{");while(!peek(p,"}")&&p->status==SLOPH_STATUS_OK){SlophSyntaxConstructor c;Vector fs={0};memset(&c,0,sizeof(c));c.span.start=p->tokens[p->index].start;c.name=copy_token(p,ident(p,true,"constructor"));take(p,"(");while(!peek(p,")")){SlophSyntaxBinder b=parse_binder(p,false,false);SlophSyntaxField f={b.name,b.type,b.span};vector_push(p,&fs,&f,sizeof(f));if(!peek(p,","))break;take(p,",");}take(p,")");c.span.end=take(p,";")->end;c.field_count=fs.count;c.fields=vector_finish(p,&fs,alignof(SlophSyntaxField));vector_push(p,&ctors,&c,sizeof(c));}d.span=span_at(start,take(p,"}")->end);d.type_parameter_count=ps.count;d.type_parameters=vector_finish(p,&ps,alignof(char*));d.constructor_count=ctors.count;d.constructors=vector_finish(p,&ctors,alignof(SlophSyntaxConstructor));vector_push(p,&types,&d,sizeof(d));}
        else if(peek(p,"fn")){SlophSyntaxFunction f;Vector ps={0},bs={0};memset(&f,0,sizeof(f));take(p,"fn");f.kind=SLOPH_SYNTAX_FUNCTION_DEFINED;f.name=copy_token(p,ident(p,false,"function"));if(peek(p,"["))parse_string_list(p,&ps,true);take(p,"(");while(!peek(p,")")){SlophSyntaxBinder b=parse_binder(p,false,true);vector_push(p,&bs,&b,sizeof(b));if(!peek(p,","))break;take(p,",");}take(p,")");take(p,"->");f.result_type=parse_type(p);if(peek(p,"|")){if(bs.count!=1u)error(p,"syntax.parse.unexpected_token","function clauses currently require exactly one parameter",span_at(start,p->tokens[p->index].end));f.body=parse_function_clauses(p,bs.count?&((SlophSyntaxBinder*)bs.data)[0]:NULL,f.result_type);}else f.body=parse_block(p,f.result_type);f.span=span_at(start,f.body?f.body->span.end:start);f.public_=pub;f.type_parameter_count=ps.count;f.type_parameters=vector_finish(p,&ps,alignof(char*));f.parameter_count=bs.count;f.parameters=vector_finish(p,&bs,alignof(SlophSyntaxBinder));vector_push(p,&functions,&f,sizeof(f));}
        else if((p->module->version==1u&&peek(p,"const"))||(p->module->version==0u&&peek(p,"value"))){SlophSyntaxValue v;memset(&v,0,sizeof(v));take(p,NULL);v.name=copy_token(p,ident(p,false,"value"));take(p,":");v.type=parse_type(p);v.value=parse_block(p,NULL);v.public_=pub;v.span=span_at(start,v.value?v.value->span.end:start);vector_push(p,&values,&v,sizeof(v));}
        else error(p,"syntax.parse.unexpected_token","expected source declaration",span_at(p->tokens[p->index].start,p->tokens[p->index].end));}
    m->span.end=p->tokens[p->index].end;m->import_count=imports.count;m->imports=vector_finish(p,&imports,alignof(SlophSyntaxImport));m->type_count=types.count;m->types=vector_finish(p,&types,alignof(SlophSyntaxTypeDecl));m->function_count=functions.count;m->functions=vector_finish(p,&functions,alignof(SlophSyntaxFunction));m->value_count=values.count;m->values=vector_finish(p,&values,alignof(SlophSyntaxValue));count_node(p,m->span);
}

SlophStatus sloph_syntax_parse(SlophContext *context,const unsigned char *source,size_t length,unsigned version,SlophSyntaxModule **out){Parser p;SlophStatus s;if(context==NULL||out==NULL||(source==NULL&&length!=0u)||version>1u)return SLOPH_STATUS_INVALID_ARGUMENT;*out=NULL;if(length>sloph_context_limits(context)->input_bytes){char msg[128];(void)snprintf(msg,sizeof(msg),"input_bytes limit exceeded (configured %zu)",sloph_context_limits(context)->input_bytes);return sloph_syntax_diagnostic(context,"syntax.parse.limit_exceeded","parse",msg,span_at(0u,0u),SLOPH_STATUS_LIMIT_EXCEEDED);}for(size_t i=0u;i<length;i++)if(source[i]>127u)return sloph_syntax_diagnostic(context,"syntax.parse.non_ascii","parse","source must contain ASCII only",span_at(i,i+1u),SLOPH_STATUS_INVALID_ARGUMENT);s=sloph_syntax_new_module(context,version,out);if(s!=SLOPH_STATUS_OK)return s;memset(&p,0,sizeof(p));p.context=context;p.module=*out;p.source=source;p.source_length=length;p.status=SLOPH_STATUS_OK;lex(&p);if(p.status==SLOPH_STATUS_OK)parse_module(&p);if(p.status==SLOPH_STATUS_OK)p.status=sloph_syntax_validate(context,*out);if(p.status!=SLOPH_STATUS_OK){sloph_syntax_module_free(*out);*out=NULL;}return p.status;}
