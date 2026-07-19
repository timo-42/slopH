#include "core_internal.h"
#include "sloph/context.h"

#include <ctype.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define sloph_core_parse sloph_core_parse_unadopted

typedef enum { SX_ATOM, SX_LIST } SxKind;
typedef struct Sx Sx;
struct Sx {
    SxKind kind;
    SlophSpan span;
    char *atom;
    Sx **items;
    size_t count;
    size_t capacity;
};

typedef struct {
    SlophContext *context;
    const SlophLimits *limits;
    const unsigned char *input;
    size_t length;
    size_t tokens;
    size_t nodes;
    SlophStatus status;
} Parser;

static char *copy_c_string(const char *source) {
    size_t length = strlen(source);
    char *result = (char *)malloc(length + 1);
    if (result != NULL) memcpy(result, source, length + 1);
    return result;
}

static SlophStatus diagnostic(Parser *p, const char *code, const char *message,
                              SlophSpan span) {
    SlophStatus status = sloph_context_add_diagnostic(p->context, code, "parse",
                                                       message, span);
    p->status = status == SLOPH_STATUS_OK ? SLOPH_STATUS_INVALID_ARGUMENT : status;
    return p->status;
}

static SlophStatus limit_diagnostic(Parser *p, const char *name, SlophSpan span) {
    char message[128];
    size_t configured = 0;
    if (strcmp(name, "input_bytes") == 0) configured = p->limits->input_bytes;
    else if (strcmp(name, "tokens") == 0) configured = p->limits->tokens;
    else if (strcmp(name, "token_bytes") == 0) configured = p->limits->token_bytes;
    else if (strcmp(name, "syntax_depth") == 0) configured = p->limits->syntax_depth;
    else if (strcmp(name, "ast_nodes") == 0) configured = p->limits->ast_nodes;
    else configured = p->limits->literal_digits;
    (void)snprintf(message, sizeof(message), "%s limit exceeded (configured %zu)",
                   name, configured);
    if (sloph_context_add_diagnostic(p->context, "core.parse.limit_exceeded",
                                     "parse", message, span) != SLOPH_STATUS_OK)
        return (p->status = SLOPH_STATUS_OUT_OF_MEMORY);
    return (p->status = SLOPH_STATUS_LIMIT_EXCEEDED);
}

static void sx_destroy(Sx *node) {
    size_t i;
    if (node == NULL) return;
    for (i = 0; i < node->count; ++i) sx_destroy(node->items[i]);
    free(node->items); free(node->atom); free(node);
}

static Sx *sx_new(Parser *p, SxKind kind, SlophSpan span) {
    Sx *node;
    if (++p->nodes > p->limits->ast_nodes) {
        limit_diagnostic(p, "ast_nodes", span); return NULL;
    }
    node = (Sx *)calloc(1, sizeof(*node));
    if (node == NULL) p->status = SLOPH_STATUS_OUT_OF_MEMORY;
    else { node->kind = kind; node->span = span; }
    return node;
}

static int sx_append(Parser *p, Sx *list, Sx *item) {
    if (list->count == list->capacity) {
        size_t capacity = list->capacity == 0 ? 4 : list->capacity * 2;
        Sx **items = (Sx **)realloc(list->items, capacity * sizeof(*items));
        if (items == NULL) { p->status = SLOPH_STATUS_OUT_OF_MEMORY; return 0; }
        list->items = items; list->capacity = capacity;
    }
    list->items[list->count++] = item; return 1;
}

static int token(Parser *p, SlophSpan span) {
    if (++p->tokens > p->limits->tokens) {
        limit_diagnostic(p, "tokens", span); return 0;
    }
    return 1;
}

static Sx *parse_form(Parser *p, size_t *position, size_t depth) {
    size_t start = *position;
    Sx *node;
    if (p->input[start] == '(') {
        if (!token(p, (SlophSpan){start, start + 1})) return NULL;
        if (depth + 1 > p->limits->syntax_depth) {
            limit_diagnostic(p, "syntax_depth", (SlophSpan){start, start + 1});
            return NULL;
        }
        node = sx_new(p, SX_LIST, (SlophSpan){start, start + 1});
        if (node == NULL) return NULL;
        ++*position;
        for (;;) {
            while (*position < p->length) {
                unsigned char c = p->input[*position];
                if (c == ';') { while (*position < p->length && p->input[*position] != '\n' && p->input[*position] != '\r') ++*position; }
                else if (c == ' ' || c == '\t' || c == '\n' || c == '\r') ++*position;
                else break;
            }
            if (*position >= p->length) {
                diagnostic(p, "core.parse.unclosed_list", "unclosed list",
                           (SlophSpan){start, p->length}); sx_destroy(node); return NULL;
            }
            if (p->input[*position] == ')') {
                if (!token(p, (SlophSpan){*position, *position + 1})) { sx_destroy(node); return NULL; }
                node->span.end = ++*position; return node;
            }
            if (p->input[*position] == '(') {
                Sx *child = parse_form(p, position, depth + 1);
                if (child == NULL || !sx_append(p, node, child)) { sx_destroy(child); sx_destroy(node); return NULL; }
            } else {
                Sx *child = parse_form(p, position, depth);
                if (child == NULL || !sx_append(p, node, child)) { sx_destroy(child); sx_destroy(node); return NULL; }
            }
        }
    }
    while (*position < p->length && strchr(" \t\n\r();", p->input[*position]) == NULL) ++*position;
    if (*position == start) {
        diagnostic(p, "core.parse.invalid_byte", "invalid input byte",
                   (SlophSpan){start, start + 1}); return NULL;
    }
    if (*position - start > p->limits->token_bytes) {
        limit_diagnostic(p, "token_bytes", (SlophSpan){start, *position}); return NULL;
    }
    if (!token(p, (SlophSpan){start, *position})) return NULL;
    node = sx_new(p, SX_ATOM, (SlophSpan){start, *position});
    if (node == NULL) return NULL;
    node->atom = (char *)malloc(*position - start + 1);
    if (node->atom == NULL) { p->status = SLOPH_STATUS_OUT_OF_MEMORY; sx_destroy(node); return NULL; }
    memcpy(node->atom, p->input + start, *position - start);
    node->atom[*position - start] = '\0'; return node;
}

static Sx *parse_sexpr(Parser *p) {
    size_t i, position = 0, roots = 0;
    Sx *root = NULL;
    if (p->length > p->limits->input_bytes) {
        limit_diagnostic(p, "input_bytes", (SlophSpan){0, p->length}); return NULL;
    }
    for (i = 0; i < p->length; ++i) {
        if (p->input[i] == 0) { diagnostic(p, "core.parse.nul", "NUL is not permitted", (SlophSpan){i, i + 1}); return NULL; }
        if (p->input[i] > 127) { diagnostic(p, "core.parse.non_ascii", "Core v0 input must be ASCII", (SlophSpan){i, i + 1}); return NULL; }
        if (p->input[i] == '\r' && (i + 1 == p->length || p->input[i + 1] != '\n')) {
            diagnostic(p, "core.parse.bare_cr", "bare carriage return is not permitted", (SlophSpan){i, i + 1}); return NULL;
        }
    }
    while (position < p->length) {
        unsigned char c = p->input[position];
        if (c == ';') { while (position < p->length && p->input[position] != '\n' && p->input[position] != '\r') ++position; continue; }
        if (c == ' ' || c == '\t' || c == '\n' || c == '\r') { ++position; continue; }
        if (c == ')') {
            diagnostic(p, "core.parse.unexpected_close", "unexpected closing parenthesis", (SlophSpan){position, position + 1}); sx_destroy(root); return NULL;
        }
        ++roots;
        if (roots == 1) root = parse_form(p, &position, 0);
        else { Sx *extra = parse_form(p, &position, 0); sx_destroy(extra); }
        if (p->status != SLOPH_STATUS_OK) { sx_destroy(root); return NULL; }
    }
    if (roots != 1) {
        diagnostic(p, "core.parse.root_count", "Core input must contain exactly one root form", (SlophSpan){0, p->length}); sx_destroy(root); return NULL;
    }
    return root;
}

static char *copy_atom(Parser *p, Sx *node, const char *description) {
    char *result;
    (void)description;
    if (node->kind != SX_ATOM) {
        diagnostic(p, "core.parse.expected_atom", "expected atom", node->span); return NULL;
    }
    result = (char *)malloc(strlen(node->atom) + 1);
    if (result == NULL) p->status = SLOPH_STATUS_OUT_OF_MEMORY;
    else strcpy(result, node->atom);
    return result;
}

static int tagged(Parser *p, Sx *node, const char *expected, size_t exact,
                  size_t minimum) {
    if (node->kind != SX_LIST || node->count == 0) {
        return !diagnostic(p, "core.parse.expected_form", "expected tagged form", node->span);
    }
    if (node->items[0]->kind != SX_ATOM) {
        diagnostic(p, "core.parse.expected_atom", "form tag must be an atom", node->items[0]->span); return 0;
    }
    if (strcmp(node->items[0]->atom, expected) != 0) {
        diagnostic(p, "core.parse.unexpected_tag", "unexpected form tag", node->items[0]->span); return 0;
    }
    if ((exact && node->count != exact) || (minimum && node->count < minimum)) {
        diagnostic(p, "core.parse.wrong_arity", "wrong number of form arguments", node->span); return 0;
    }
    return 1;
}

static SlophCoreType *decode_type(Parser *p, Sx *node);
static SlophCoreExpr *decode_expr(Parser *p, Sx *node, int version);

static SlophCoreType *new_type(Parser *p, SlophCoreTypeKind kind) {
    SlophCoreType *type = (SlophCoreType *)calloc(1, sizeof(*type));
    if (type == NULL) p->status = SLOPH_STATUS_OUT_OF_MEMORY;
    else type->kind = kind;
    return type;
}

static SlophCoreType *decode_type(Parser *p, Sx *node) {
    SlophCoreType *type = NULL;
    size_t i;
    if (node->kind == SX_ATOM) {
        if (strcmp(node->atom, "Int") == 0) return new_type(p, SLOPH_TYPE_INT);
        if (strcmp(node->atom, "Bytes") == 0) return new_type(p, SLOPH_TYPE_BYTES);
        diagnostic(p, "core.parse.unknown_type", "unknown type form", node->span); return NULL;
    }
    if (node->count == 0) { diagnostic(p, "core.parse.empty_type", "empty type form", node->span); return NULL; }
    if (node->items[0]->kind != SX_ATOM) { diagnostic(p, "core.parse.expected_atom", "type tag must be an atom", node->items[0]->span); return NULL; }
    if (strcmp(node->items[0]->atom, "named") == 0 || strcmp(node->items[0]->atom, "var") == 0) {
        int named = node->items[0]->atom[0] == 'n';
        if (!tagged(p, node, named ? "named" : "var", 2, 0)) return NULL;
        type = new_type(p, named ? SLOPH_TYPE_NAMED : SLOPH_TYPE_VARIABLE);
        if (type != NULL) type->as.name = copy_atom(p, node->items[1], "type name");
    } else if (strcmp(node->items[0]->atom, "apply") == 0) {
        if (!tagged(p, node, "apply", 0, 3)) return NULL;
        type = new_type(p, SLOPH_TYPE_APPLIED); if (type == NULL) return NULL;
        type->as.applied.constructor = copy_atom(p, node->items[1], "constructor");
        type->as.applied.count = node->count - 2;
        type->as.applied.items = (SlophCoreType **)calloc(type->as.applied.count, sizeof(*type->as.applied.items));
        if (type->as.applied.items == NULL) p->status = SLOPH_STATUS_OUT_OF_MEMORY;
        for (i = 0; p->status == SLOPH_STATUS_OK && i < type->as.applied.count; ++i)
            type->as.applied.items[i] = decode_type(p, node->items[i + 2]);
    } else if (strcmp(node->items[0]->atom, "fn") == 0) {
        size_t offset;
        if (node->count != 3 && node->count != 4) { diagnostic(p, "core.parse.arity", "function type expects an optional ownership mode and two types", node->span); return NULL; }
        type = new_type(p, SLOPH_TYPE_FUNCTION); if (type == NULL) return NULL;
        offset = node->count == 4 ? 2 : 1;
        type->as.function.mode = node->count == 4 ? copy_atom(p, node->items[1], "parameter mode") : copy_c_string("own");
        if (type->as.function.mode == NULL) p->status = SLOPH_STATUS_OUT_OF_MEMORY;
        if (type->as.function.mode != NULL && strcmp(type->as.function.mode, "own") && strcmp(type->as.function.mode, "borrow") && strcmp(type->as.function.mode, "borrow-mut"))
            diagnostic(p, "core.parse.parameter_mode", "parameter mode must be own, borrow, or borrow-mut", node->items[1]->span);
        if (p->status == SLOPH_STATUS_OK) type->as.function.parameter = decode_type(p, node->items[offset]);
        if (p->status == SLOPH_STATUS_OK) type->as.function.result = decode_type(p, node->items[offset + 1]);
    } else if (strcmp(node->items[0]->atom, "forall") == 0) {
        if (!tagged(p, node, "forall", 3, 0)) return NULL;
        type = new_type(p, SLOPH_TYPE_FORALL); if (type == NULL) return NULL;
        type->as.forall.parameter = copy_atom(p, node->items[1], "type parameter");
        if (p->status == SLOPH_STATUS_OK) type->as.forall.body = decode_type(p, node->items[2]);
    } else diagnostic(p, "core.parse.unknown_type", "unknown type tag", node->items[0]->span);
    if (p->status != SLOPH_STATUS_OK) { sloph_core_type_destroy(type); return NULL; }
    return type;
}

static int decode_binder(Parser *p, Sx *node, SlophCoreBinder *out) {
    size_t offset = 1;
    memset(out, 0, sizeof(*out)); out->span = node->span;
    if (node->kind == SX_LIST && node->count && node->items[0]->kind == SX_ATOM && strcmp(node->items[0]->atom, "type-bind") == 0) {
        if (!tagged(p, node, "type-bind", 2, 0)) return 0;
        out->is_type = 1; out->name = copy_atom(p, node->items[1], "type binder"); return out->name != NULL;
    }
    if (node->kind != SX_LIST || (node->count != 3 && node->count != 4) || !tagged(p, node, "bind", 0, 3)) return 0;
    if (node->count == 4) offset = 2;
    out->mode = node->count == 4 ? copy_atom(p, node->items[1], "binder mode") : copy_c_string("own");
    if (out->mode == NULL) p->status = SLOPH_STATUS_OUT_OF_MEMORY;
    if (out->mode != NULL && strcmp(out->mode, "own") && strcmp(out->mode, "borrow") && strcmp(out->mode, "borrow-mut")) {
        diagnostic(p, "core.parse.parameter_mode", "binder mode must be own, borrow, or borrow-mut", node->items[1]->span); return 0;
    }
    out->name = copy_atom(p, node->items[offset], "binder name");
    if (p->status == SLOPH_STATUS_OK) out->type = decode_type(p, node->items[offset + 1]);
    return p->status == SLOPH_STATUS_OK;
}

static int integer_valid(const char *text) {
    const char *p = text;
    if (*p == '-') ++p;
    if (*p == '\0') return 0;
    if (*p == '0') return p[1] == '\0';
    if (*p < '1' || *p > '9') return 0;
    while (*++p) if (!isdigit((unsigned char)*p)) return 0;
    return 1;
}

static SlophCoreExpr *new_expr(Parser *p, SlophCoreExprKind kind, SlophSpan span) {
    SlophCoreExpr *expr = (SlophCoreExpr *)calloc(1, sizeof(*expr));
    if (expr == NULL) p->status = SLOPH_STATUS_OUT_OF_MEMORY;
    else { expr->kind = kind; expr->span = span; }
    return expr;
}

static int hex_value(char c) { return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1; }

static SlophCoreExpr *decode_expr(Parser *p, Sx *node, int version) {
    SlophCoreExpr *expr = NULL; const char *tag; size_t i;
    if (node->kind != SX_LIST || node->count == 0) { diagnostic(p, "core.parse.expression_form", "expression must be a non-empty tagged list", node->span); return NULL; }
    if (node->items[0]->kind != SX_ATOM) { diagnostic(p, "core.parse.expected_atom", "expression tag must be an atom", node->items[0]->span); return NULL; }
    tag = node->items[0]->atom;
    if (!strcmp(tag, "int")) {
        char *value;
        if (!tagged(p, node, "int", 2, 0)) return NULL;
        value = copy_atom(p, node->items[1], "integer literal"); if (value == NULL) return NULL;
        if (!integer_valid(value)) { free(value); diagnostic(p, "core.parse.integer_syntax", "integer literals use canonical decimal syntax", node->items[1]->span); return NULL; }
        if (strlen(value) - (value[0] == '-') > p->limits->literal_digits) { free(value); limit_diagnostic(p, "literal_digits", node->items[1]->span); return NULL; }
        if (!strcmp(value, "-0")) { value[0] = '0'; value[1] = '\0'; }
        expr = new_expr(p, SLOPH_EXPR_INT, node->span); if (expr) expr->as.integer = value; else free(value);
    } else if (!strcmp(tag, "bytes")) {
        char *value; size_t length;
        if (!tagged(p, node, "bytes", 2, 0)) return NULL;
        value = copy_atom(p, node->items[1], "byte literal"); if (!value) return NULL; length = strlen(value);
        if (length == 0 || value[0] != 'x' || (length - 1) % 2) { free(value); diagnostic(p, "core.parse.byte_syntax", "bytes use 'x' followed by canonical lowercase hexadecimal", node->items[1]->span); return NULL; }
        expr = new_expr(p, SLOPH_EXPR_BYTES, node->span);
        if (expr) { expr->as.bytes.length = (length - 1) / 2; expr->as.bytes.data = (unsigned char *)malloc(expr->as.bytes.length ? expr->as.bytes.length : 1); if (!expr->as.bytes.data) p->status = SLOPH_STATUS_OUT_OF_MEMORY; }
        for (i = 0; p->status == SLOPH_STATUS_OK && i < (length - 1) / 2; ++i) { int a=hex_value(value[1+i*2]), b=hex_value(value[2+i*2]); if(a<0||b<0){diagnostic(p,"core.parse.byte_syntax","bytes use 'x' followed by canonical lowercase hexadecimal",node->items[1]->span);break;} expr->as.bytes.data[i]=(unsigned char)(a*16+b); }
        free(value);
    } else if (!strcmp(tag, "local") || !strcmp(tag, "global")) {
        if (!tagged(p, node, tag, 2, 0)) return NULL;
        expr = new_expr(p, tag[0]=='l'?SLOPH_EXPR_LOCAL:SLOPH_EXPR_GLOBAL, node->span); if(expr) expr->as.name=copy_atom(p,node->items[1],"name");
    } else if (!strcmp(tag, "type")) {
        if (!tagged(p, node, "type", 2, 0)) return NULL;
        expr = new_expr(p, SLOPH_EXPR_TYPE, node->span);
        if (expr) expr->as.type = decode_type(p, node->items[1]);
    } else if (!strcmp(tag, "lam")) {
        if (!tagged(p, node, "lam", 3, 0)) return NULL;
        expr = new_expr(p, SLOPH_EXPR_LAM, node->span);
        if (expr && decode_binder(p, node->items[1], &expr->as.lam.binder))
            expr->as.lam.body = decode_expr(p, node->items[2], version);
    } else if (!strcmp(tag, "app")) {
        if (!tagged(p, node, "app", 3, 0)) return NULL;
        expr = new_expr(p, SLOPH_EXPR_APP, node->span);
        if (expr) expr->as.app.function = decode_expr(p, node->items[1], version);
        if (p->status == SLOPH_STATUS_OK)
            expr->as.app.argument = decode_expr(p, node->items[2], version);
    } else if (!strcmp(tag, "let")) {
        if (!tagged(p, node, "let", 4, 0)) return NULL;
        expr = new_expr(p, SLOPH_EXPR_LET, node->span);
        if (expr && decode_binder(p, node->items[1], &expr->as.let.binder))
            expr->as.let.value = decode_expr(p, node->items[2], version);
        if (p->status == SLOPH_STATUS_OK)
            expr->as.let.body = decode_expr(p, node->items[3], version);
    } else if (!strcmp(tag, "prim")) {
        if (!tagged(p, node, "prim", 0, 2)) return NULL;
        expr = new_expr(p, SLOPH_EXPR_PRIM, node->span);
        if(expr){expr->as.prim.name=copy_atom(p,node->items[1],"primitive name");expr->as.prim.count=node->count-2;expr->as.prim.items=(SlophCoreExpr**)calloc(expr->as.prim.count?expr->as.prim.count:1,sizeof(*expr->as.prim.items));if(!expr->as.prim.items)p->status=SLOPH_STATUS_OUT_OF_MEMORY;}
        for(i=0;p->status==SLOPH_STATUS_OK&&i<expr->as.prim.count;++i)expr->as.prim.items[i]=decode_expr(p,node->items[i+2],version);
    } else if (!strcmp(tag, "con")) {
        size_t offset=version>=2?3:2;
        if (!tagged(p, node, "con", 0, version >= 2 ? 3 : 2)) return NULL;
        expr = new_expr(p, SLOPH_EXPR_CON, node->span);
        if(expr)expr->as.con.constructor=copy_atom(p,node->items[1],"constructor identity");
        if(version>=2&&p->status==SLOPH_STATUS_OK){Sx*types=node->items[2];if(!tagged(p,types,"types",0,1)){}else{expr->as.con.type_argument_count=types->count-1;expr->as.con.type_arguments=(SlophCoreType**)calloc(expr->as.con.type_argument_count?expr->as.con.type_argument_count:1,sizeof(*expr->as.con.type_arguments));if(!expr->as.con.type_arguments)p->status=SLOPH_STATUS_OUT_OF_MEMORY;for(i=0;p->status==SLOPH_STATUS_OK&&i<expr->as.con.type_argument_count;++i)expr->as.con.type_arguments[i]=decode_type(p,types->items[i+1]);}}
        if(expr){expr->as.con.field_count=node->count-offset;expr->as.con.fields=(SlophCoreExpr**)calloc(expr->as.con.field_count?expr->as.con.field_count:1,sizeof(*expr->as.con.fields));if(!expr->as.con.fields)p->status=SLOPH_STATUS_OUT_OF_MEMORY;for(i=0;p->status==SLOPH_STATUS_OK&&i<expr->as.con.field_count;++i)expr->as.con.fields[i]=decode_expr(p,node->items[i+offset],version);}
    } else if (!strcmp(tag, "case")) {
        if (!tagged(p, node, "case", 0, 3)) return NULL;
        expr = new_expr(p, SLOPH_EXPR_CASE, node->span);
        if (expr)
            expr->as.case_.scrutinee = decode_expr(p, node->items[1], version);
        if (p->status == SLOPH_STATUS_OK)
            expr->as.case_.result_type = decode_type(p, node->items[2]);
        if(expr){expr->as.case_.alternative_count=node->count-3;expr->as.case_.alternatives=(SlophCoreAlternative*)calloc(expr->as.case_.alternative_count?expr->as.case_.alternative_count:1,sizeof(*expr->as.case_.alternatives));if(!expr->as.case_.alternatives)p->status=SLOPH_STATUS_OUT_OF_MEMORY;}
        for(i=0;p->status==SLOPH_STATUS_OK&&i<expr->as.case_.alternative_count;++i){Sx*a=node->items[i+3];SlophCoreAlternative*out=&expr->as.case_.alternatives[i];size_t j;if(!tagged(p,a,"alt",0,3))break;out->span=a->span;out->constructor=copy_atom(p,a->items[1],"alternative constructor");out->binder_count=a->count-3;out->binders=(SlophCoreBinder*)calloc(out->binder_count?out->binder_count:1,sizeof(*out->binders));if(!out->binders){p->status=SLOPH_STATUS_OUT_OF_MEMORY;break;}for(j=0;p->status==SLOPH_STATUS_OK&&j<out->binder_count;++j)decode_binder(p,a->items[j+2],&out->binders[j]);if(p->status==SLOPH_STATUS_OK)out->body=decode_expr(p,a->items[a->count-1],version);}
    } else diagnostic(p,"core.parse.unknown_expression","unknown expression tag",node->items[0]->span);
    if(p->status!=SLOPH_STATUS_OK){sloph_core_expr_destroy(expr);return NULL;}return expr;
}

static int decode_field(Parser*p,Sx*n,SlophCoreField*out){if(!tagged(p,n,"field",3,0))return 0;out->span=n->span;out->name=copy_atom(p,n->items[1],"field name");if(p->status==SLOPH_STATUS_OK)out->type=decode_type(p,n->items[2]);if(out->type&&out->type->kind==SLOPH_TYPE_FUNCTION)diagnostic(p,"core.parse.function_field","Core v0 nominal fields cannot have function type",n->items[2]->span);return p->status==SLOPH_STATUS_OK;}

static int decode_enum(Parser*p,Sx*n,int version,SlophCoreEnum*out){size_t offset=2,i,j;if(!tagged(p,n,"enum",0,version>=2?3:2))return 0;out->span=n->span;out->name=copy_atom(p,n->items[1],"enum identity");if(version==3){Sx*own=n->items[2];if(!tagged(p,own,"ownership",2,0))return 0;if(own->items[1]->kind!=SX_ATOM)return !diagnostic(p,"core.parse.expected_atom","ownership mode must be an atom",own->items[1]->span);if(strcmp(own->items[1]->atom,"copy")&&strcmp(own->items[1]->atom,"owned"))return !diagnostic(p,"core.parse.ownership_mode","ownership mode must be copy or owned",own->items[1]->span);out->owned=!strcmp(own->items[1]->atom,"owned");++offset;}if(version>=2){Sx*params=n->items[offset++];if(!tagged(p,params,"params",0,1))return 0;out->type_parameter_count=params->count-1;out->type_parameters=(char**)calloc(out->type_parameter_count?out->type_parameter_count:1,sizeof(*out->type_parameters));if(!out->type_parameters){p->status=SLOPH_STATUS_OUT_OF_MEMORY;return 0;}for(i=0;i<out->type_parameter_count;++i)if(!(out->type_parameters[i]=copy_atom(p,params->items[i+1],"type parameter")))return 0;}out->constructor_count=n->count-offset;out->constructors=(SlophCoreConstructor*)calloc(out->constructor_count?out->constructor_count:1,sizeof(*out->constructors));if(!out->constructors){p->status=SLOPH_STATUS_OUT_OF_MEMORY;return 0;}for(i=0;p->status==SLOPH_STATUS_OK&&i<out->constructor_count;++i){Sx*c=n->items[offset+i];SlophCoreConstructor*co=&out->constructors[i];if(!tagged(p,c,"ctor",0,2))break;co->span=c->span;co->name=copy_atom(p,c->items[1],"constructor identity");co->field_count=c->count-2;co->fields=(SlophCoreField*)calloc(co->field_count?co->field_count:1,sizeof(*co->fields));if(!co->fields){p->status=SLOPH_STATUS_OUT_OF_MEMORY;break;}for(j=0;p->status==SLOPH_STATUS_OK&&j<co->field_count;++j)decode_field(p,c->items[j+2],&co->fields[j]);}return p->status==SLOPH_STATUS_OK;}

static char *percent_decode(Parser *p, Sx *node) {
    char *raw = copy_atom(p, node, "encoded string"), *result;
    size_t read = 0, write = 0, length;
    if (!raw) return NULL;
    length = strlen(raw); result = (char *)malloc(length + 1);
    if (!result) { free(raw); p->status = SLOPH_STATUS_OUT_OF_MEMORY; return NULL; }
    while (read < length) {
        if (raw[read] == '%' && read + 2 < length) {
            int a = hex_value(raw[read + 1]), b = hex_value(raw[read + 2]);
            if (a >= 0 && b >= 0) { result[write++] = (char)(a * 16 + b); read += 3; continue; }
        }
        result[write++] = raw[read++];
    }
    result[write] = 0; free(raw); return result;
}

static int atom_array(Parser *p, Sx *form, char ***out, size_t *count,
                      int decode) {
    size_t i; *count = form->count - 1;
    *out = (char **)calloc(*count ? *count : 1, sizeof(**out));
    if (!*out) { p->status = SLOPH_STATUS_OUT_OF_MEMORY; return 0; }
    for (i = 0; i < *count; ++i) {
        (*out)[i] = decode ? percent_decode(p, form->items[i + 1])
                           : copy_atom(p, form->items[i + 1], "list item");
        if (!(*out)[i]) return 0;
    }
    return 1;
}

static int decode_foreign(Parser *p, Sx *n, SlophCoreForeignBinding *out) {
    Sx *params, *result, *cparams, *cresult, *provider, *header, *requires,
       *effects, *facts, *provenance;
    size_t i;
    if (!tagged(p, n, "binding", 14, 0)) return 0;
    params=n->items[4];result=n->items[5];cparams=n->items[6];cresult=n->items[7];
    provider=n->items[8];header=n->items[9];requires=n->items[10];effects=n->items[11];facts=n->items[12];provenance=n->items[13];
    if(!tagged(p,params,"params",0,1)||!tagged(p,result,"result",2,0)||
       !tagged(p,cparams,"c-params",0,1)||!tagged(p,cresult,"c-result",2,0)||
       !tagged(p,provider,"provider",2,0)||!tagged(p,header,"header",2,0)||
       !tagged(p,requires,"requires",0,1)||!tagged(p,effects,"effects",0,1)||
       !tagged(p,facts,"facts",0,1)||!tagged(p,provenance,"provenance",2,0)) return 0;
    out->identity=copy_atom(p,n->items[1],"binding identity");
    out->symbol=copy_atom(p,n->items[2],"C symbol");out->adapter=copy_atom(p,n->items[3],"adapter");
    out->parameter_count=params->count-1;out->parameters=(SlophCoreType**)calloc(out->parameter_count?out->parameter_count:1,sizeof(*out->parameters));if(!out->parameters){p->status=SLOPH_STATUS_OUT_OF_MEMORY;return 0;}
    for(i=0;p->status==SLOPH_STATUS_OK&&i<out->parameter_count;++i)out->parameters[i]=decode_type(p,params->items[i+1]);
    if(p->status==SLOPH_STATUS_OK)out->result=decode_type(p,result->items[1]);
    if(p->status==SLOPH_STATUS_OK)atom_array(p,cparams,&out->c_parameters,&out->c_parameter_count,1);
    if(p->status==SLOPH_STATUS_OK)out->c_result=percent_decode(p,cresult->items[1]);
    if(p->status==SLOPH_STATUS_OK)out->provider=copy_atom(p,provider->items[1],"provider");
    if(p->status==SLOPH_STATUS_OK)out->header=percent_decode(p,header->items[1]);
    if(p->status==SLOPH_STATUS_OK)atom_array(p,requires,&out->requires,&out->require_count,0);
    if(p->status==SLOPH_STATUS_OK)atom_array(p,effects,&out->effects,&out->effect_count,0);
    out->fact_count=facts->count-1;out->facts=(SlophCoreFact*)calloc(out->fact_count?out->fact_count:1,sizeof(*out->facts));if(!out->facts){p->status=SLOPH_STATUS_OUT_OF_MEMORY;return 0;}
    for(i=0;p->status==SLOPH_STATUS_OK&&i<out->fact_count;++i){Sx*f=facts->items[i+1];if(!tagged(p,f,"fact",3,0))break;out->facts[i].key=copy_atom(p,f->items[1],"fact key");out->facts[i].value=copy_atom(p,f->items[2],"fact value");}
    if(p->status==SLOPH_STATUS_OK)out->provenance=copy_atom(p,provenance->items[1],"provenance");
    return p->status==SLOPH_STATUS_OK;
}

static int decode_unit(Parser*p,Sx*n,SlophCoreUnit**out){SlophCoreUnit*u;Sx*types,*defs;size_t i;if(!tagged(p,n,"core",0,4))return 0;if(n->count!=4&&n->count!=5)return !diagnostic(p,"core.parse.arity","core form must contain types, defs, and optional foreign bindings",n->span);if(n->items[1]->kind!=SX_ATOM)return !diagnostic(p,"core.parse.expected_atom","Core version must be an atom",n->items[1]->span);if(strcmp(n->items[1]->atom,"0")&&strcmp(n->items[1]->atom,"1")&&strcmp(n->items[1]->atom,"2")&&strcmp(n->items[1]->atom,"3"))return !diagnostic(p,"core.parse.unsupported_version","only Core versions 0, 1, 2, and 3 are supported",n->items[1]->span);types=n->items[2];defs=n->items[3];if(!tagged(p,types,"types",0,1)||!tagged(p,defs,"defs",0,1))return 0;u=(SlophCoreUnit*)calloc(1,sizeof(*u));if(!u){p->status=SLOPH_STATUS_OUT_OF_MEMORY;return 0;}u->span=n->span;u->version=atoi(n->items[1]->atom);u->type_count=types->count-1;u->types=(SlophCoreEnum*)calloc(u->type_count?u->type_count:1,sizeof(*u->types));u->definition_count=defs->count-1;u->definitions=(SlophCoreDefinition*)calloc(u->definition_count?u->definition_count:1,sizeof(*u->definitions));if(!u->types||!u->definitions)p->status=SLOPH_STATUS_OUT_OF_MEMORY;for(i=0;p->status==SLOPH_STATUS_OK&&i<u->type_count;++i)decode_enum(p,types->items[i+1],u->version,&u->types[i]);for(i=0;p->status==SLOPH_STATUS_OK&&i<u->definition_count;++i){Sx*d=defs->items[i+1];SlophCoreDefinition*o=&u->definitions[i];if(!tagged(p,d,"def",4,0))break;o->span=d->span;o->name=copy_atom(p,d->items[1],"definition identity");if(p->status==SLOPH_STATUS_OK)o->type=decode_type(p,d->items[2]);if(p->status==SLOPH_STATUS_OK)o->value=decode_expr(p,d->items[3],u->version);}if(n->count==5&&p->status==SLOPH_STATUS_OK){Sx*f=n->items[4];if(tagged(p,f,"foreign",0,1)){u->foreign_binding_count=f->count-1;u->foreign_bindings=(SlophCoreForeignBinding*)calloc(u->foreign_binding_count?u->foreign_binding_count:1,sizeof(*u->foreign_bindings));if(!u->foreign_bindings)p->status=SLOPH_STATUS_OUT_OF_MEMORY;for(i=0;p->status==SLOPH_STATUS_OK&&i<u->foreign_binding_count;++i)decode_foreign(p,f->items[i+1],&u->foreign_bindings[i]);}}if(p->status!=SLOPH_STATUS_OK){sloph_core_free(u);return 0;}*out=u;return 1;}

SlophStatus sloph_core_parse(SlophContext *context, const unsigned char *input,
                             size_t input_length, SlophCoreUnit **out_unit) {
    Parser parser; Sx *root;
    if(context==NULL||out_unit==NULL||(input==NULL&&input_length)){return SLOPH_STATUS_INVALID_ARGUMENT;}
    *out_unit=NULL;memset(&parser,0,sizeof(parser));parser.context=context;parser.limits=sloph_context_limits(context);parser.input=input;parser.length=input_length;parser.status=SLOPH_STATUS_OK;
    root=parse_sexpr(&parser);if(root==NULL)return parser.status;
    (void)decode_unit(&parser,root,out_unit);sx_destroy(root);return parser.status;
}

#undef sloph_core_parse
SlophStatus sloph_core_parse(SlophContext *context,
                             const unsigned char *input,
                             size_t input_length,
                             SlophCoreUnit **out_unit) {
    SlophStatus status = sloph_core_parse_unadopted(
        context, input, input_length, out_unit);
    if (status == SLOPH_STATUS_OK) {
        status = sloph_core_adopt_allocator(context, out_unit);
        if (status != SLOPH_STATUS_OK) {
            sloph_core_free(*out_unit);
            *out_unit = NULL;
        }
    }
    return status;
}
