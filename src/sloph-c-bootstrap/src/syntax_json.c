#include "syntax_internal.h"
#include "internal.h"
#include "yyjson.h"

#include <stdlib.h>
#include <stdalign.h>
#include <string.h>

static unsigned char json_hex_value(char character) {
    if (character >= '0' && character <= '9')
        return (unsigned char)(character - '0');
    if (character >= 'a' && character <= 'f')
        return (unsigned char)(character - 'a' + 10);
    return (unsigned char)(character - 'A' + 10);
}
static bool json_object_keys(yyjson_val *object, const char *const *allowed,
                             size_t allowed_count) {
    yyjson_obj_iter iterator;
    yyjson_val *key;
    size_t count = 0u;
    if (!yyjson_is_obj(object)) return false;
    iterator = yyjson_obj_iter_with(object);
    while ((key = yyjson_obj_iter_next(&iterator)) != NULL) {
        const char *name = yyjson_get_str(key);
        size_t i;
        bool found = false;
        for (i = 0u; i < allowed_count; ++i)
            if (strcmp(name, allowed[i]) == 0) { found = true; break; }
        if (!found) return false;
        ++count;
    }
    return count == allowed_count;
}

static yyjson_mut_val *span_(yyjson_mut_doc*d,SlophSpan s){yyjson_mut_val*o=yyjson_mut_obj(d);yyjson_mut_obj_add_uint(d,o,"end",s.end);yyjson_mut_obj_add_uint(d,o,"start",s.start);return o;}
static yyjson_mut_val *strings(yyjson_mut_doc*d,char*const*p,size_t n){size_t i;yyjson_mut_val*a=yyjson_mut_arr(d);for(i=0;i<n;i++)yyjson_mut_arr_add_str(d,a,p[i]);return a;}
static yyjson_mut_val *type_node(yyjson_mut_doc*d,const SlophSyntaxModule*m,const SlophSyntaxType*t){size_t i;yyjson_mut_val*o=yyjson_mut_obj(d),*a;(void)m;switch(t->kind){case SLOPH_SYNTAX_TYPE_INT:yyjson_mut_obj_add_str(d,o,"kind","IntType");break;case SLOPH_SYNTAX_TYPE_NAMED:yyjson_mut_obj_add_str(d,o,"kind","NamedType");yyjson_mut_obj_add_str(d,o,"name",t->as.name);break;case SLOPH_SYNTAX_TYPE_APPLIED:a=yyjson_mut_arr(d);for(i=0;i<t->as.applied.count;i++)yyjson_mut_arr_add_val(a,type_node(d,m,t->as.applied.items[i]));yyjson_mut_obj_add_val(d,o,"arguments",a);yyjson_mut_obj_add_str(d,o,"constructor",t->as.applied.constructor);break;case SLOPH_SYNTAX_TYPE_FUNCTION:yyjson_mut_obj_add_str(d,o,"kind","FunctionType");yyjson_mut_obj_add_str(d,o,"mode",t->as.function.mode);yyjson_mut_obj_add_val(d,o,"parameter",type_node(d,m,t->as.function.parameter));yyjson_mut_obj_add_val(d,o,"result",type_node(d,m,t->as.function.result));break;case SLOPH_SYNTAX_TYPE_INFERRED:yyjson_mut_obj_add_str(d,o,"kind","InferredType");break;}if(t->kind==SLOPH_SYNTAX_TYPE_APPLIED)yyjson_mut_obj_add_str(d,o,"kind","AppliedType");yyjson_mut_obj_add_val(d,o,"span",span_(d,t->span));return o;}
static yyjson_mut_val *binder_node(yyjson_mut_doc*d,const SlophSyntaxModule*m,const SlophSyntaxBinder*b){yyjson_mut_val*o=yyjson_mut_obj(d);yyjson_mut_obj_add_str(d,o,"kind","Binder");yyjson_mut_obj_add_str(d,o,"mode",b->mode);yyjson_mut_obj_add_str(d,o,"name",b->name);yyjson_mut_obj_add_val(d,o,"span",span_(d,b->span));yyjson_mut_obj_add_val(d,o,"type",type_node(d,m,b->type));return o;}
static yyjson_mut_val *block_node(yyjson_mut_doc*d,const SlophSyntaxModule*m,const SlophSyntaxBlock*b);
static yyjson_mut_val *expr_node(yyjson_mut_doc*d,const SlophSyntaxModule*m,const SlophSyntaxExpr*e){size_t i;yyjson_mut_val*o=yyjson_mut_obj(d),*a;switch(e->kind){case SLOPH_SYNTAX_EXPR_INT:yyjson_mut_obj_add_str(d,o,"kind","IntExpr");yyjson_mut_obj_add_val(d,o,"span",span_(d,e->span));yyjson_mut_obj_add_str(d,o,"value",e->as.integer);return o;case SLOPH_SYNTAX_EXPR_BYTES:{static const char h[]="0123456789abcdef";char*x=malloc(e->as.bytes.length*2u+1u);if(!x)return NULL;for(i=0;i<e->as.bytes.length;i++){x[i*2u]=h[e->as.bytes.data[i]>>4];x[i*2u+1u]=h[e->as.bytes.data[i]&15u];}x[e->as.bytes.length*2u]=0;yyjson_mut_obj_add_strcpy(d,o,"hex",x);free(x);yyjson_mut_obj_add_str(d,o,"encoding",e->as.bytes.kind==SLOPH_SYNTAX_BYTES_ASCII?"ascii":(e->as.bytes.kind==SLOPH_SYNTAX_BYTES_UTF8?"utf8":"bytes"));yyjson_mut_obj_add_str(d,o,"kind","BytesExpr");break;}case SLOPH_SYNTAX_EXPR_LOCAL:case SLOPH_SYNTAX_EXPR_GLOBAL:yyjson_mut_obj_add_str(d,o,"kind",e->kind==SLOPH_SYNTAX_EXPR_LOCAL?"LocalExpr":"GlobalExpr");yyjson_mut_obj_add_str(d,o,"name",e->as.name);break;case SLOPH_SYNTAX_EXPR_CALL:a=yyjson_mut_arr(d);for(i=0;i<e->as.call.argument_count;i++)yyjson_mut_arr_add_val(a,expr_node(d,m,e->as.call.arguments[i]));yyjson_mut_obj_add_val(d,o,"arguments",a);yyjson_mut_obj_add_val(d,o,"function",expr_node(d,m,e->as.call.function));yyjson_mut_obj_add_str(d,o,"kind","CallExpr");a=yyjson_mut_arr(d);for(i=0;i<e->as.call.type_argument_count;i++)yyjson_mut_arr_add_val(a,type_node(d,m,e->as.call.type_arguments[i]));yyjson_mut_obj_add_val(d,o,"type_arguments",a);break;case SLOPH_SYNTAX_EXPR_BINARY:yyjson_mut_obj_add_str(d,o,"kind","BinaryExpr");yyjson_mut_obj_add_val(d,o,"left",expr_node(d,m,e->as.binary.left));yyjson_mut_obj_add_str(d,o,"operator",e->as.binary.operator_);yyjson_mut_obj_add_val(d,o,"right",expr_node(d,m,e->as.binary.right));break;case SLOPH_SYNTAX_EXPR_LAMBDA:yyjson_mut_obj_add_val(d,o,"body",block_node(d,m,e->as.lambda.body));yyjson_mut_obj_add_str(d,o,"kind","LambdaExpr");a=yyjson_mut_arr(d);for(i=0;i<e->as.lambda.parameter_count;i++)yyjson_mut_arr_add_val(a,binder_node(d,m,&e->as.lambda.parameters[i]));yyjson_mut_obj_add_val(d,o,"parameters",a);yyjson_mut_obj_add_val(d,o,"result_type",type_node(d,m,e->as.lambda.result_type));break;case SLOPH_SYNTAX_EXPR_IF:yyjson_mut_obj_add_val(d,o,"condition",expr_node(d,m,e->as.if_.condition));yyjson_mut_obj_add_val(d,o,"else_body",block_node(d,m,e->as.if_.else_body));yyjson_mut_obj_add_str(d,o,"kind","IfExpr");yyjson_mut_obj_add_val(d,o,"span",span_(d,e->span));yyjson_mut_obj_add_val(d,o,"then_body",block_node(d,m,e->as.if_.then_body));return o;case SLOPH_SYNTAX_EXPR_CONSTRUCTOR:a=yyjson_mut_arr(d);for(i=0;i<e->as.constructor.argument_count;i++)yyjson_mut_arr_add_val(a,expr_node(d,m,e->as.constructor.arguments[i]));yyjson_mut_obj_add_val(d,o,"arguments",a);yyjson_mut_obj_add_str(d,o,"constructor",e->as.constructor.constructor);yyjson_mut_obj_add_str(d,o,"kind","ConstructorExpr");a=yyjson_mut_arr(d);for(i=0;i<e->as.constructor.type_argument_count;i++)yyjson_mut_arr_add_val(a,type_node(d,m,e->as.constructor.type_arguments[i]));yyjson_mut_obj_add_val(d,o,"type_arguments",a);break;case SLOPH_SYNTAX_EXPR_PRIMITIVE:a=yyjson_mut_arr(d);for(i=0;i<e->as.primitive.argument_count;i++)yyjson_mut_arr_add_val(a,expr_node(d,m,e->as.primitive.arguments[i]));yyjson_mut_obj_add_val(d,o,"arguments",a);yyjson_mut_obj_add_str(d,o,"kind","PrimitiveExpr");yyjson_mut_obj_add_str(d,o,"name",e->as.primitive.name);break;case SLOPH_SYNTAX_EXPR_CASE:a=yyjson_mut_arr(d);for(i=0;i<e->as.case_.alternative_count;i++){size_t j;SlophSyntaxCaseAlternative*x=&e->as.case_.alternatives[i];yyjson_mut_val*z=yyjson_mut_obj(d),*bs=yyjson_mut_arr(d);for(j=0;j<x->binder_count;j++)yyjson_mut_arr_add_val(bs,binder_node(d,m,&x->binders[j]));yyjson_mut_obj_add_val(d,z,"binders",bs);yyjson_mut_obj_add_val(d,z,"body",block_node(d,m,x->body));yyjson_mut_obj_add_str(d,z,"constructor",x->constructor);yyjson_mut_obj_add_str(d,z,"kind","CaseAlternative");yyjson_mut_obj_add_val(d,z,"span",span_(d,x->span));yyjson_mut_arr_add_val(a,z);}yyjson_mut_obj_add_val(d,o,"alternatives",a);yyjson_mut_obj_add_str(d,o,"kind","CaseExpr");yyjson_mut_obj_add_val(d,o,"result_type",type_node(d,m,e->as.case_.result_type));yyjson_mut_obj_add_val(d,o,"scrutinee",expr_node(d,m,e->as.case_.scrutinee));break;}yyjson_mut_obj_add_val(d,o,"span",span_(d,e->span));return o;}
static yyjson_mut_val *block_node(yyjson_mut_doc*d,const SlophSyntaxModule*m,const SlophSyntaxBlock*b){size_t i;yyjson_mut_val*o=yyjson_mut_obj(d),*a=yyjson_mut_arr(d);for(i=0;i<b->statement_count;i++){SlophSyntaxStatement*s=&b->statements[i];yyjson_mut_val*z=yyjson_mut_obj(d);if(s->kind==SLOPH_SYNTAX_STMT_LET){yyjson_mut_obj_add_val(d,z,"binder",binder_node(d,m,&s->as.let.binder));yyjson_mut_obj_add_str(d,z,"kind","LetBinding");yyjson_mut_obj_add_val(d,z,"span",span_(d,s->span));yyjson_mut_obj_add_val(d,z,"value",expr_node(d,m,s->as.let.value));}else{yyjson_mut_obj_add_val(d,z,"call",expr_node(d,m,s->as.defer_call));yyjson_mut_obj_add_str(d,z,"kind","DeferCall");yyjson_mut_obj_add_val(d,z,"span",span_(d,s->span));}yyjson_mut_arr_add_val(a,z);}yyjson_mut_obj_add_val(d,o,"bindings",a);yyjson_mut_obj_add_str(d,o,"kind","Block");yyjson_mut_obj_add_val(d,o,"result",expr_node(d,m,b->result));yyjson_mut_obj_add_val(d,o,"span",span_(d,b->span));return o;}

static yyjson_mut_val *pattern_node(yyjson_mut_doc *doc,
                                    const SlophSyntaxPattern *pattern) {
    size_t i; yyjson_mut_val *object = yyjson_mut_obj(doc);
    if (pattern->kind == SLOPH_SYNTAX_PATTERN_CONSTANT) {
        yyjson_mut_obj_add_str(doc, object, "kind", "TargetConstantPattern");
        yyjson_mut_obj_add_str(doc, object, "name", pattern->as.constant);
    } else {
        yyjson_mut_val *items = yyjson_mut_arr(doc);
        for (i = 0u; i < pattern->as.tuple.count; ++i)
            yyjson_mut_arr_add_val(items, pattern_node(doc, pattern->as.tuple.items[i]));
        yyjson_mut_obj_add_val(doc, object, "items", items);
        yyjson_mut_obj_add_str(doc, object, "kind", "TargetTuplePattern");
    }
    yyjson_mut_obj_add_val(doc, object, "span", span_(doc, pattern->span));
    return object;
}
static yyjson_mut_val *direct_import_node(yyjson_mut_doc *doc,
                                          const SlophSyntaxDirectImport *import_) {
    yyjson_mut_val *object = yyjson_mut_obj(doc);
    yyjson_mut_obj_add_str(doc, object, "kind", "ImportDecl");
    yyjson_mut_obj_add_str(doc, object, "module", import_->module);
    yyjson_mut_obj_add_val(doc, object, "names",
                           strings(doc, import_->names, import_->name_count));
    if (import_->public_) yyjson_mut_obj_add_bool(doc, object, "public", true);
    yyjson_mut_obj_add_val(doc, object, "span", span_(doc, import_->span));
    return object;
}
static yyjson_mut_val *module_node(yyjson_mut_doc *doc,
                                   const SlophSyntaxModule *module) {
    size_t i, j, k; yyjson_mut_val *object = yyjson_mut_obj(doc), *array;
    if (module->availability != NULL) {
        yyjson_mut_val *availability = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, availability, "kind", "Availability");
        yyjson_mut_obj_add_val(doc, availability, "pattern",
                               pattern_node(doc, module->availability->pattern));
        yyjson_mut_obj_add_str(doc, availability, "selector",
                               module->availability->selector);
        yyjson_mut_obj_add_val(doc, availability, "span",
                               span_(doc, module->availability->span));
        yyjson_mut_obj_add_val(doc, object, "availability", availability);
    }
    array = yyjson_mut_arr(doc);
    for (i = 0u; i < module->function_count; ++i) {
        SlophSyntaxFunction *function = &module->functions[i];
        yyjson_mut_val *item = yyjson_mut_obj(doc), *parameters = yyjson_mut_arr(doc);
        if (function->kind == SLOPH_SYNTAX_FUNCTION_FOREIGN)
            yyjson_mut_obj_add_str(doc, item, "binding", function->binding);
        if (function->kind == SLOPH_SYNTAX_FUNCTION_DEFINED)
            yyjson_mut_obj_add_val(doc, item, "body", block_node(doc, module, function->body));
        if (function->kind == SLOPH_SYNTAX_FUNCTION_INTRINSIC)
            yyjson_mut_obj_add_str(doc, item, "intrinsic", function->binding);
        yyjson_mut_obj_add_str(doc, item, "kind",
            function->kind == SLOPH_SYNTAX_FUNCTION_DEFINED ? "FunctionDecl" :
            (function->kind == SLOPH_SYNTAX_FUNCTION_FOREIGN ? "ForeignFunctionDecl" : "IntrinsicFunctionDecl"));
        yyjson_mut_obj_add_str(doc, item, "name", function->name);
        for (j = 0u; j < function->parameter_count; ++j)
            yyjson_mut_arr_add_val(parameters, binder_node(doc, module, &function->parameters[j]));
        yyjson_mut_obj_add_val(doc, item, "parameters", parameters);
        yyjson_mut_obj_add_bool(doc, item, "public", function->public_);
        yyjson_mut_obj_add_val(doc, item, "result_type", type_node(doc, module, function->result_type));
        yyjson_mut_obj_add_val(doc, item, "span", span_(doc, function->span));
        if (function->kind == SLOPH_SYNTAX_FUNCTION_DEFINED)
            yyjson_mut_obj_add_val(doc, item, "type_parameters",
                                   strings(doc, function->type_parameters, function->type_parameter_count));
        yyjson_mut_arr_add_val(array, item);
    }
    yyjson_mut_obj_add_val(doc, object, "functions", array);
    array = yyjson_mut_arr(doc);
    for (i = 0u; i < module->import_count; ++i) {
        SlophSyntaxImport *import_ = &module->imports[i];
        if (import_->kind == SLOPH_SYNTAX_IMPORT_DIRECT)
            yyjson_mut_arr_add_val(array, direct_import_node(doc, &import_->as.direct));
        else {
            yyjson_mut_val *item = yyjson_mut_obj(doc), *alternatives = yyjson_mut_arr(doc);
            for (j = 0u; j < import_->as.conditional.alternative_count; ++j) {
                SlophSyntaxConditionalAlternative *alternative =
                    &import_->as.conditional.alternatives[j];
                yyjson_mut_val *encoded = yyjson_mut_obj(doc);
                yyjson_mut_obj_add_val(doc, encoded, "import",
                                       direct_import_node(doc, &alternative->import_));
                yyjson_mut_obj_add_str(doc, encoded, "kind",
                                       "ConditionalImportAlternative");
                yyjson_mut_obj_add_val(doc, encoded, "pattern",
                                       pattern_node(doc, alternative->pattern));
                yyjson_mut_obj_add_val(doc, encoded, "span",
                                       span_(doc, alternative->span));
                yyjson_mut_arr_add_val(alternatives, encoded);
            }
            yyjson_mut_obj_add_val(doc, item, "alternatives", alternatives);
            yyjson_mut_obj_add_str(doc, item, "kind", "ConditionalImportDecl");
            yyjson_mut_obj_add_str(doc, item, "selector",
                                   import_->as.conditional.selector);
            yyjson_mut_obj_add_val(doc, item, "span", span_(doc, import_->span));
            yyjson_mut_arr_add_val(array, item);
        }
    }
    yyjson_mut_obj_add_val(doc, object, "imports", array);
    yyjson_mut_obj_add_str(doc, object, "kind", "Module");
    yyjson_mut_obj_add_str(doc, object, "name", module->name);
    yyjson_mut_obj_add_val(doc, object, "span", span_(doc, module->span));
    array = yyjson_mut_arr(doc);
    for (i = 0u; i < module->type_count; ++i) {
        SlophSyntaxTypeDecl *type = &module->types[i];
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        if (type->kind == SLOPH_SYNTAX_TYPE_DECL_ENUM) {
            yyjson_mut_val *constructors = yyjson_mut_arr(doc);
            for (j = 0u; j < type->constructor_count; ++j) {
                yyjson_mut_val *constructor = yyjson_mut_obj(doc);
                yyjson_mut_val *fields = yyjson_mut_arr(doc);
                for (k = 0u; k < type->constructors[j].field_count; ++k) {
                    yyjson_mut_val *field = yyjson_mut_obj(doc);
                    yyjson_mut_obj_add_str(doc, field, "kind", "FieldDecl");
                    yyjson_mut_obj_add_str(doc, field, "name",
                                           type->constructors[j].fields[k].name);
                    yyjson_mut_obj_add_val(doc, field, "span",
                                           span_(doc, type->constructors[j].fields[k].span));
                    yyjson_mut_obj_add_val(doc, field, "type",
                                           type_node(doc, module, type->constructors[j].fields[k].type));
                    yyjson_mut_arr_add_val(fields, field);
                }
                yyjson_mut_obj_add_val(doc, constructor, "fields", fields);
                yyjson_mut_obj_add_str(doc, constructor, "kind", "ConstructorDecl");
                yyjson_mut_obj_add_str(doc, constructor, "name", type->constructors[j].name);
                yyjson_mut_obj_add_val(doc, constructor, "span",
                                       span_(doc, type->constructors[j].span));
                yyjson_mut_arr_add_val(constructors, constructor);
            }
            yyjson_mut_obj_add_val(doc, item, "constructors", constructors);
        }
        yyjson_mut_obj_add_str(doc, item, "kind",
            type->kind == SLOPH_SYNTAX_TYPE_DECL_INTRINSIC ? "IntrinsicTypeDecl" : "TypeDecl");
        yyjson_mut_obj_add_str(doc, item, "name", type->name);
        if (type->owned) yyjson_mut_obj_add_bool(doc, item, "owned", true);
        yyjson_mut_obj_add_bool(doc, item, "public", type->public_);
        yyjson_mut_obj_add_val(doc, item, "span", span_(doc, type->span));
        if (type->kind == SLOPH_SYNTAX_TYPE_DECL_ENUM)
            yyjson_mut_obj_add_val(doc, item, "type_parameters",
                                   strings(doc, type->type_parameters, type->type_parameter_count));
        yyjson_mut_arr_add_val(array, item);
    }
    yyjson_mut_obj_add_val(doc, object, "types", array);
    array = yyjson_mut_arr(doc);
    for (i = 0u; i < module->value_count; ++i) {
        SlophSyntaxValue *value = &module->values[i];
        yyjson_mut_val *item = yyjson_mut_obj(doc);
        yyjson_mut_obj_add_str(doc, item, "kind", "ValueDecl");
        yyjson_mut_obj_add_str(doc, item, "name", value->name);
        yyjson_mut_obj_add_bool(doc, item, "public", value->public_);
        yyjson_mut_obj_add_val(doc, item, "span", span_(doc, value->span));
        yyjson_mut_obj_add_val(doc, item, "type", type_node(doc, module, value->type));
        yyjson_mut_obj_add_val(doc, item, "value", block_node(doc, module, value->value));
        yyjson_mut_arr_add_val(array, item);
    }
    yyjson_mut_obj_add_val(doc, object, "values", array);
    return object;
}
SlophStatus sloph_syntax_to_json(SlophContext*c,const SlophSyntaxModule*m,SlophSyntaxText*out){yyjson_mut_doc*d;yyjson_mut_val*r;char*raw,*copy;size_t n;const SlophAllocator*a;SlophYyjsonAllocator ya;if(c==NULL||m==NULL||out==NULL)return SLOPH_STATUS_INVALID_ARGUMENT;out->data=NULL;out->length=0;sloph_yyjson_allocator_init(&ya,c);d=yyjson_mut_doc_new(&ya.interface);if(!d)return SLOPH_STATUS_OUT_OF_MEMORY;r=yyjson_mut_obj(d);yyjson_mut_obj_add_val(d,r,"module",module_node(d,m));yyjson_mut_obj_add_str(d,r,"schema","sloph.syntax");yyjson_mut_obj_add_uint(d,r,"version",m->version);yyjson_mut_doc_set_root(d,r);raw=yyjson_mut_write_opts(d,0u,&ya.interface,&n,NULL);yyjson_mut_doc_free(d);if(!raw)return SLOPH_STATUS_OUT_OF_MEMORY;if(n+1u>sloph_context_limits(c)->output_bytes){ya.interface.free(ya.interface.ctx,raw);return sloph_syntax_diagnostic(c,"syntax.json.limit_exceeded","json","output_bytes limit exceeded",SLOPH_UNKNOWN_SPAN,SLOPH_STATUS_LIMIT_EXCEEDED);}a=sloph_context_allocator(c);copy=a->allocate(a->user_data,n+2u);if(!copy){ya.interface.free(ya.interface.ctx,raw);return SLOPH_STATUS_OUT_OF_MEMORY;}memcpy(copy,raw,n);copy[n]='\n';copy[n+1u]=0;ya.interface.free(ya.interface.ctx,raw);out->data=copy;out->length=n+1u;return SLOPH_STATUS_OK;}

static SlophSpan decode_span(yyjson_val *object) {
    yyjson_val *span = yyjson_obj_get(object, "span");
    SlophSpan result = {0u, 0u};
    if (yyjson_is_obj(span)) {
        result.start = (size_t)yyjson_get_uint(yyjson_obj_get(span, "start"));
        result.end = (size_t)yyjson_get_uint(yyjson_obj_get(span, "end"));
    }
    return result;
}
static char *decode_string(SlophSyntaxModule *module, yyjson_val *value) {
    char *result = NULL;
    if (yyjson_is_str(value))
        (void)sloph_syntax_string(module, yyjson_get_str(value),
                                  yyjson_get_len(value), &result);
    return result;
}
static SlophSyntaxType *decode_type(SlophSyntaxModule *module, yyjson_val *value);
static SlophSyntaxExpr *decode_expr(SlophSyntaxModule *module, yyjson_val *value);
static SlophSyntaxBlock *decode_block(SlophSyntaxModule *module, yyjson_val *value);
static SlophSyntaxType *decode_type(SlophSyntaxModule *module, yyjson_val *value) {
    const char *kind = yyjson_get_str(yyjson_obj_get(value, "kind"));
    SlophSyntaxType *type = NULL;
    yyjson_val *array, *item; yyjson_arr_iter iterator; size_t index = 0u;
    if (sloph_syntax_alloc(module, sizeof(*type), alignof(SlophSyntaxType),
                           (void **)&type) != SLOPH_STATUS_OK) return NULL;
    type->span = decode_span(value);
    if (kind != NULL && strcmp(kind, "IntType") == 0) type->kind = SLOPH_SYNTAX_TYPE_INT;
    else if (kind != NULL && strcmp(kind, "NamedType") == 0) {
        type->kind = SLOPH_SYNTAX_TYPE_NAMED;
        type->as.name = decode_string(module, yyjson_obj_get(value, "name"));
    } else if (kind != NULL && strcmp(kind, "InferredType") == 0)
        type->kind = SLOPH_SYNTAX_TYPE_INFERRED;
    else if (kind != NULL && strcmp(kind, "AppliedType") == 0) {
        type->kind = SLOPH_SYNTAX_TYPE_APPLIED;
        type->as.applied.constructor = decode_string(module, yyjson_obj_get(value, "constructor"));
        array = yyjson_obj_get(value, "arguments");
        type->as.applied.count = yyjson_arr_size(array);
        if (type->as.applied.count != 0u)
            (void)sloph_syntax_alloc(module, type->as.applied.count * sizeof(*type->as.applied.items),
                alignof(SlophSyntaxType *), (void **)&type->as.applied.items);
        iterator = yyjson_arr_iter_with(array);
        while ((item = yyjson_arr_iter_next(&iterator)) != NULL)
            type->as.applied.items[index++] = decode_type(module, item);
    } else if (kind != NULL && strcmp(kind, "FunctionType") == 0) {
        type->kind = SLOPH_SYNTAX_TYPE_FUNCTION;
        type->as.function.mode = decode_string(module, yyjson_obj_get(value, "mode"));
        if (type->as.function.mode == NULL) type->as.function.mode = "own";
        type->as.function.parameter = decode_type(module, yyjson_obj_get(value, "parameter"));
        type->as.function.result = decode_type(module, yyjson_obj_get(value, "result"));
    }
    return type;
}
static SlophSyntaxBinder decode_binder(SlophSyntaxModule *module, yyjson_val *value) {
    SlophSyntaxBinder binder; memset(&binder, 0, sizeof(binder));
    binder.name = decode_string(module, yyjson_obj_get(value, "name"));
    binder.mode = decode_string(module, yyjson_obj_get(value, "mode"));
    if (binder.mode == NULL) binder.mode = "own";
    binder.type = decode_type(module, yyjson_obj_get(value, "type"));
    binder.span = decode_span(value); return binder;
}
static SlophSyntaxExpr **decode_expr_array(SlophSyntaxModule *module,
                                           yyjson_val *array, size_t *count) {
    SlophSyntaxExpr **result = NULL; yyjson_arr_iter it; yyjson_val *item; size_t i=0u;
    *count=yyjson_arr_size(array);
    if(*count)(void)sloph_syntax_alloc(module,*count*sizeof(*result),alignof(SlophSyntaxExpr*),(void**)&result);
    it=yyjson_arr_iter_with(array);while((item=yyjson_arr_iter_next(&it))!=NULL)result[i++]=decode_expr(module,item);
    return result;
}
static SlophSyntaxType **decode_type_array(SlophSyntaxModule *module,
                                           yyjson_val *array, size_t *count) {
    SlophSyntaxType **result=NULL;yyjson_arr_iter it;yyjson_val*item;size_t i=0u;*count=yyjson_arr_size(array);
    if(*count)(void)sloph_syntax_alloc(module,*count*sizeof(*result),alignof(SlophSyntaxType*),(void**)&result);
    it=yyjson_arr_iter_with(array);while((item=yyjson_arr_iter_next(&it))!=NULL)result[i++]=decode_type(module,item);return result;
}
static SlophSyntaxExpr *decode_expr(SlophSyntaxModule *module, yyjson_val *value) {
    const char *kind=yyjson_get_str(yyjson_obj_get(value,"kind"));SlophSyntaxExpr*e=NULL;yyjson_val*array,*item;yyjson_arr_iter it;size_t i=0u;
    if(sloph_syntax_alloc(module,sizeof(*e),alignof(SlophSyntaxExpr),(void**)&e)!=SLOPH_STATUS_OK)
        return NULL;
    e->span=decode_span(value);
    if(kind&&strcmp(kind,"IntExpr")==0){e->kind=SLOPH_SYNTAX_EXPR_INT;e->as.integer=decode_string(module,yyjson_obj_get(value,"value"));}
    else if(kind&&strcmp(kind,"BytesExpr")==0){const char*hex=yyjson_get_str(yyjson_obj_get(value,"hex"));size_t n=hex?strlen(hex)/2u:0u;e->kind=SLOPH_SYNTAX_EXPR_BYTES;{const char*encoding=yyjson_get_str(yyjson_obj_get(value,"encoding"));e->as.bytes.kind=encoding&&strcmp(encoding,"ascii")==0?SLOPH_SYNTAX_BYTES_ASCII:(encoding&&strcmp(encoding,"utf8")==0?SLOPH_SYNTAX_BYTES_UTF8:SLOPH_SYNTAX_BYTES_RAW);}e->as.bytes.length=n;if(n)(void)sloph_syntax_alloc(module,n,alignof(unsigned char),(void**)&e->as.bytes.data);for(i=0u;i<n;i++)e->as.bytes.data[i]=(unsigned char)((json_hex_value(hex[i*2u])<<4)|json_hex_value(hex[i*2u+1u]));}
    else if(kind&&(strcmp(kind,"LocalExpr")==0||strcmp(kind,"GlobalExpr")==0)){e->kind=strcmp(kind,"LocalExpr")==0?SLOPH_SYNTAX_EXPR_LOCAL:SLOPH_SYNTAX_EXPR_GLOBAL;e->as.name=decode_string(module,yyjson_obj_get(value,"name"));}
    else if(kind&&strcmp(kind,"CallExpr")==0){e->kind=SLOPH_SYNTAX_EXPR_CALL;e->as.call.function=decode_expr(module,yyjson_obj_get(value,"function"));e->as.call.arguments=decode_expr_array(module,yyjson_obj_get(value,"arguments"),&e->as.call.argument_count);e->as.call.type_arguments=decode_type_array(module,yyjson_obj_get(value,"type_arguments"),&e->as.call.type_argument_count);}
    else if(kind&&strcmp(kind,"BinaryExpr")==0){e->kind=SLOPH_SYNTAX_EXPR_BINARY;e->as.binary.operator_=decode_string(module,yyjson_obj_get(value,"operator"));e->as.binary.left=decode_expr(module,yyjson_obj_get(value,"left"));e->as.binary.right=decode_expr(module,yyjson_obj_get(value,"right"));}
    else if(kind&&strcmp(kind,"IfExpr")==0){e->kind=SLOPH_SYNTAX_EXPR_IF;e->as.if_.condition=decode_expr(module,yyjson_obj_get(value,"condition"));e->as.if_.then_body=decode_block(module,yyjson_obj_get(value,"then_body"));e->as.if_.else_body=decode_block(module,yyjson_obj_get(value,"else_body"));}
    else if(kind&&strcmp(kind,"ConstructorExpr")==0){e->kind=SLOPH_SYNTAX_EXPR_CONSTRUCTOR;e->as.constructor.constructor=decode_string(module,yyjson_obj_get(value,"constructor"));e->as.constructor.arguments=decode_expr_array(module,yyjson_obj_get(value,"arguments"),&e->as.constructor.argument_count);e->as.constructor.type_arguments=decode_type_array(module,yyjson_obj_get(value,"type_arguments"),&e->as.constructor.type_argument_count);}
    else if(kind&&strcmp(kind,"PrimitiveExpr")==0){e->kind=SLOPH_SYNTAX_EXPR_PRIMITIVE;e->as.primitive.name=decode_string(module,yyjson_obj_get(value,"name"));e->as.primitive.arguments=decode_expr_array(module,yyjson_obj_get(value,"arguments"),&e->as.primitive.argument_count);}
    else if(kind&&strcmp(kind,"LambdaExpr")==0){e->kind=SLOPH_SYNTAX_EXPR_LAMBDA;array=yyjson_obj_get(value,"parameters");e->as.lambda.parameter_count=yyjson_arr_size(array);if(e->as.lambda.parameter_count)(void)sloph_syntax_alloc(module,e->as.lambda.parameter_count*sizeof(*e->as.lambda.parameters),alignof(SlophSyntaxBinder),(void**)&e->as.lambda.parameters);it=yyjson_arr_iter_with(array);while((item=yyjson_arr_iter_next(&it))!=NULL)e->as.lambda.parameters[i++]=decode_binder(module,item);e->as.lambda.result_type=decode_type(module,yyjson_obj_get(value,"result_type"));e->as.lambda.body=decode_block(module,yyjson_obj_get(value,"body"));}
    else if(kind&&strcmp(kind,"CaseExpr")==0){e->kind=SLOPH_SYNTAX_EXPR_CASE;e->as.case_.scrutinee=decode_expr(module,yyjson_obj_get(value,"scrutinee"));e->as.case_.result_type=decode_type(module,yyjson_obj_get(value,"result_type"));array=yyjson_obj_get(value,"alternatives");e->as.case_.alternative_count=yyjson_arr_size(array);if(e->as.case_.alternative_count)(void)sloph_syntax_alloc(module,e->as.case_.alternative_count*sizeof(*e->as.case_.alternatives),alignof(SlophSyntaxCaseAlternative),(void**)&e->as.case_.alternatives);it=yyjson_arr_iter_with(array);while((item=yyjson_arr_iter_next(&it))!=NULL){SlophSyntaxCaseAlternative*a=&e->as.case_.alternatives[i++];yyjson_val*bs=yyjson_obj_get(item,"binders"),*b;yyjson_arr_iter bi;size_t j=0u;a->constructor=decode_string(module,yyjson_obj_get(item,"constructor"));a->span=decode_span(item);a->body=decode_block(module,yyjson_obj_get(item,"body"));a->binder_count=yyjson_arr_size(bs);if(a->binder_count)(void)sloph_syntax_alloc(module,a->binder_count*sizeof(*a->binders),alignof(SlophSyntaxBinder),(void**)&a->binders);bi=yyjson_arr_iter_with(bs);while((b=yyjson_arr_iter_next(&bi))!=NULL)a->binders[j++]=decode_binder(module,b);}}
    return e;
}
static SlophSyntaxBlock *decode_block(SlophSyntaxModule *module, yyjson_val *value) {
    SlophSyntaxBlock*b=NULL;yyjson_val*array,*item;yyjson_arr_iter it;size_t i=0u;(void)sloph_syntax_alloc(module,sizeof(*b),alignof(SlophSyntaxBlock),(void**)&b);b->span=decode_span(value);b->result=decode_expr(module,yyjson_obj_get(value,"result"));array=yyjson_obj_get(value,"bindings");b->statement_count=yyjson_arr_size(array);if(b->statement_count)(void)sloph_syntax_alloc(module,b->statement_count*sizeof(*b->statements),alignof(SlophSyntaxStatement),(void**)&b->statements);it=yyjson_arr_iter_with(array);while((item=yyjson_arr_iter_next(&it))!=NULL){const char*k=yyjson_get_str(yyjson_obj_get(item,"kind"));SlophSyntaxStatement*s=&b->statements[i++];s->span=decode_span(item);if(k&&strcmp(k,"LetBinding")==0){s->kind=SLOPH_SYNTAX_STMT_LET;s->as.let.binder=decode_binder(module,yyjson_obj_get(item,"binder"));s->as.let.value=decode_expr(module,yyjson_obj_get(item,"value"));}else{s->kind=SLOPH_SYNTAX_STMT_DEFER;s->as.defer_call=decode_expr(module,yyjson_obj_get(item,"call"));}}return b;
}

static SlophSyntaxPattern *decode_pattern(SlophSyntaxModule *module, yyjson_val *value) {
    SlophSyntaxPattern *pattern=NULL;const char*kind=yyjson_get_str(yyjson_obj_get(value,"kind"));yyjson_val*item,*items;yyjson_arr_iter it;size_t i=0u;
    (void)sloph_syntax_alloc(module,sizeof(*pattern),alignof(SlophSyntaxPattern),(void**)&pattern);pattern->span=decode_span(value);
    if(kind&&strcmp(kind,"TargetTuplePattern")==0){pattern->kind=SLOPH_SYNTAX_PATTERN_TUPLE;items=yyjson_obj_get(value,"items");pattern->as.tuple.count=yyjson_arr_size(items);if(pattern->as.tuple.count)(void)sloph_syntax_alloc(module,pattern->as.tuple.count*sizeof(*pattern->as.tuple.items),alignof(SlophSyntaxPattern*),(void**)&pattern->as.tuple.items);it=yyjson_arr_iter_with(items);while((item=yyjson_arr_iter_next(&it))!=NULL)pattern->as.tuple.items[i++]=decode_pattern(module,item);}else{pattern->kind=SLOPH_SYNTAX_PATTERN_CONSTANT;pattern->as.constant=decode_string(module,yyjson_obj_get(value,"name"));}return pattern;
}
static SlophSyntaxDirectImport decode_direct_import(SlophSyntaxModule *module,yyjson_val*value){
    SlophSyntaxDirectImport d;yyjson_val*names,*item;yyjson_arr_iter it;size_t i=0u;memset(&d,0,sizeof(d));d.module=decode_string(module,yyjson_obj_get(value,"module"));d.public_=yyjson_get_bool(yyjson_obj_get(value,"public"));d.span=decode_span(value);names=yyjson_obj_get(value,"names");d.name_count=yyjson_arr_size(names);if(d.name_count)(void)sloph_syntax_alloc(module,d.name_count*sizeof(*d.names),alignof(char*),(void**)&d.names);it=yyjson_arr_iter_with(names);while((item=yyjson_arr_iter_next(&it))!=NULL)d.names[i++]=decode_string(module,item);return d;
}
static char **decode_strings(SlophSyntaxModule*m,yyjson_val*a,size_t*n){char**r=NULL;yyjson_val*x;yyjson_arr_iter it;size_t i=0u;*n=yyjson_arr_size(a);if(*n)(void)sloph_syntax_alloc(m,*n*sizeof(*r),alignof(char*),(void**)&r);it=yyjson_arr_iter_with(a);while((x=yyjson_arr_iter_next(&it))!=NULL)r[i++]=decode_string(m,x);return r;}
static SlophStatus decode_module_json(SlophSyntaxModule *module, yyjson_val *value) {
    yyjson_val *array,*item;yyjson_arr_iter it;size_t i=0u,j;module->name=decode_string(module,yyjson_obj_get(value,"name"));module->span=decode_span(value);
    item=yyjson_obj_get(value,"availability");if(yyjson_is_obj(item)){(void)sloph_syntax_alloc(module,sizeof(*module->availability),alignof(SlophSyntaxAvailability),(void**)&module->availability);module->availability->selector=decode_string(module,yyjson_obj_get(item,"selector"));module->availability->pattern=decode_pattern(module,yyjson_obj_get(item,"pattern"));module->availability->span=decode_span(item);}
    array=yyjson_obj_get(value,"imports");module->import_count=yyjson_arr_size(array);if(module->import_count)(void)sloph_syntax_alloc(module,module->import_count*sizeof(*module->imports),alignof(SlophSyntaxImport),(void**)&module->imports);it=yyjson_arr_iter_with(array);while((item=yyjson_arr_iter_next(&it))!=NULL){const char*k=yyjson_get_str(yyjson_obj_get(item,"kind"));SlophSyntaxImport*x=&module->imports[i++];x->span=decode_span(item);if(k&&strcmp(k,"ConditionalImportDecl")==0){yyjson_val*alts,*a;yyjson_arr_iter ai;size_t q=0u;x->kind=SLOPH_SYNTAX_IMPORT_CONDITIONAL;x->as.conditional.selector=decode_string(module,yyjson_obj_get(item,"selector"));alts=yyjson_obj_get(item,"alternatives");x->as.conditional.alternative_count=yyjson_arr_size(alts);if(x->as.conditional.alternative_count)(void)sloph_syntax_alloc(module,x->as.conditional.alternative_count*sizeof(*x->as.conditional.alternatives),alignof(SlophSyntaxConditionalAlternative),(void**)&x->as.conditional.alternatives);ai=yyjson_arr_iter_with(alts);while((a=yyjson_arr_iter_next(&ai))!=NULL){SlophSyntaxConditionalAlternative*z=&x->as.conditional.alternatives[q++];z->pattern=decode_pattern(module,yyjson_obj_get(a,"pattern"));z->import_=decode_direct_import(module,yyjson_obj_get(a,"import"));z->span=decode_span(a);}}else{x->kind=SLOPH_SYNTAX_IMPORT_DIRECT;x->as.direct=decode_direct_import(module,item);}}
    i=0u;array=yyjson_obj_get(value,"types");module->type_count=yyjson_arr_size(array);if(module->type_count)(void)sloph_syntax_alloc(module,module->type_count*sizeof(*module->types),alignof(SlophSyntaxTypeDecl),(void**)&module->types);it=yyjson_arr_iter_with(array);while((item=yyjson_arr_iter_next(&it))!=NULL){const char*k=yyjson_get_str(yyjson_obj_get(item,"kind"));SlophSyntaxTypeDecl*t=&module->types[i++];t->kind=k&&strcmp(k,"IntrinsicTypeDecl")==0?SLOPH_SYNTAX_TYPE_DECL_INTRINSIC:SLOPH_SYNTAX_TYPE_DECL_ENUM;t->name=decode_string(module,yyjson_obj_get(item,"name"));t->public_=yyjson_get_bool(yyjson_obj_get(item,"public"));t->owned=yyjson_get_bool(yyjson_obj_get(item,"owned"));t->span=decode_span(item);t->type_parameters=decode_strings(module,yyjson_obj_get(item,"type_parameters"),&t->type_parameter_count);if(t->kind==SLOPH_SYNTAX_TYPE_DECL_ENUM){yyjson_val*ctors,*c;yyjson_arr_iter ci;size_t q=0u;ctors=yyjson_obj_get(item,"constructors");t->constructor_count=yyjson_arr_size(ctors);if(t->constructor_count)(void)sloph_syntax_alloc(module,t->constructor_count*sizeof(*t->constructors),alignof(SlophSyntaxConstructor),(void**)&t->constructors);ci=yyjson_arr_iter_with(ctors);while((c=yyjson_arr_iter_next(&ci))!=NULL){SlophSyntaxConstructor*z=&t->constructors[q++];yyjson_val*fs,*f;yyjson_arr_iter fi;size_t w=0u;z->name=decode_string(module,yyjson_obj_get(c,"name"));z->span=decode_span(c);fs=yyjson_obj_get(c,"fields");z->field_count=yyjson_arr_size(fs);if(z->field_count)(void)sloph_syntax_alloc(module,z->field_count*sizeof(*z->fields),alignof(SlophSyntaxField),(void**)&z->fields);fi=yyjson_arr_iter_with(fs);while((f=yyjson_arr_iter_next(&fi))!=NULL){z->fields[w].name=decode_string(module,yyjson_obj_get(f,"name"));z->fields[w].type=decode_type(module,yyjson_obj_get(f,"type"));z->fields[w++].span=decode_span(f);}}}}
    i=0u;array=yyjson_obj_get(value,"functions");module->function_count=yyjson_arr_size(array);if(module->function_count)(void)sloph_syntax_alloc(module,module->function_count*sizeof(*module->functions),alignof(SlophSyntaxFunction),(void**)&module->functions);it=yyjson_arr_iter_with(array);while((item=yyjson_arr_iter_next(&it))!=NULL){const char*k=yyjson_get_str(yyjson_obj_get(item,"kind"));SlophSyntaxFunction*f=&module->functions[i++];yyjson_val*ps,*p;yyjson_arr_iter pi;size_t q=0u;f->kind=k&&strcmp(k,"ForeignFunctionDecl")==0?SLOPH_SYNTAX_FUNCTION_FOREIGN:(k&&strcmp(k,"IntrinsicFunctionDecl")==0?SLOPH_SYNTAX_FUNCTION_INTRINSIC:SLOPH_SYNTAX_FUNCTION_DEFINED);f->name=decode_string(module,yyjson_obj_get(item,"name"));f->public_=yyjson_get_bool(yyjson_obj_get(item,"public"));f->span=decode_span(item);f->result_type=decode_type(module,yyjson_obj_get(item,"result_type"));f->binding=decode_string(module,yyjson_obj_get(item,f->kind==SLOPH_SYNTAX_FUNCTION_FOREIGN?"binding":"intrinsic"));f->body=f->kind==SLOPH_SYNTAX_FUNCTION_DEFINED?decode_block(module,yyjson_obj_get(item,"body")):NULL;f->type_parameters=decode_strings(module,yyjson_obj_get(item,"type_parameters"),&f->type_parameter_count);ps=yyjson_obj_get(item,"parameters");f->parameter_count=yyjson_arr_size(ps);if(f->parameter_count)(void)sloph_syntax_alloc(module,f->parameter_count*sizeof(*f->parameters),alignof(SlophSyntaxBinder),(void**)&f->parameters);pi=yyjson_arr_iter_with(ps);while((p=yyjson_arr_iter_next(&pi))!=NULL)f->parameters[q++]=decode_binder(module,p);}
    i=0u;array=yyjson_obj_get(value,"values");module->value_count=yyjson_arr_size(array);if(module->value_count)(void)sloph_syntax_alloc(module,module->value_count*sizeof(*module->values),alignof(SlophSyntaxValue),(void**)&module->values);it=yyjson_arr_iter_with(array);while((item=yyjson_arr_iter_next(&it))!=NULL){SlophSyntaxValue*v=&module->values[i++];v->name=decode_string(module,yyjson_obj_get(item,"name"));v->public_=yyjson_get_bool(yyjson_obj_get(item,"public"));v->span=decode_span(item);v->type=decode_type(module,yyjson_obj_get(item,"type"));v->value=decode_block(module,yyjson_obj_get(item,"value"));}
    (void)j;return SLOPH_STATUS_OK;
}

SlophStatus sloph_syntax_from_json(SlophContext*c,const unsigned char*j,size_t n,unsigned v,SlophSyntaxModule**out){yyjson_doc*d;yyjson_val*r,*schema,*version,*module_value;SlophStatus status;SlophYyjsonAllocator ya;if(c==NULL||out==NULL||(j==NULL&&n)||v>1u)return SLOPH_STATUS_INVALID_ARGUMENT;*out=NULL;if(n>sloph_context_limits(c)->input_bytes)return sloph_syntax_diagnostic(c,"syntax.json.limit_exceeded","json","input_bytes limit exceeded",SLOPH_UNKNOWN_SPAN,SLOPH_STATUS_LIMIT_EXCEEDED);sloph_yyjson_allocator_init(&ya,c);d=yyjson_read_opts((char*)(void*)j,n,0u,&ya.interface,NULL);if(!d)return sloph_syntax_diagnostic(c,"syntax.json.invalid","json","invalid JSON",SLOPH_UNKNOWN_SPAN,SLOPH_STATUS_INVALID_ARGUMENT);r=yyjson_doc_get_root(d);{static const char*const root_keys[]={"schema","version","module"};if(!json_object_keys(r,root_keys,3u)){yyjson_doc_free(d);return sloph_syntax_diagnostic(c,"syntax.json.invalid","json","object has missing or unknown fields",SLOPH_UNKNOWN_SPAN,SLOPH_STATUS_INVALID_ARGUMENT);}}schema=yyjson_is_obj(r)?yyjson_obj_get(r,"schema"):NULL;version=yyjson_is_obj(r)?yyjson_obj_get(r,"version"):NULL;module_value=yyjson_is_obj(r)?yyjson_obj_get(r,"module"):NULL;if(!yyjson_is_str(schema)||strcmp(yyjson_get_str(schema),"sloph.syntax")||!yyjson_is_uint(version)||yyjson_get_uint(version)!=v||!yyjson_is_obj(module_value)){yyjson_doc_free(d);return sloph_syntax_diagnostic(c,"syntax.json.unsupported_schema","json","unsupported syntax schema",SLOPH_UNKNOWN_SPAN,SLOPH_STATUS_INVALID_ARGUMENT);}{static const char*const module_keys[]={"kind","name","imports","types","functions","values","span"};static const char*const available_keys[]={"availability","kind","name","imports","types","functions","values","span"};bool has_availability=yyjson_obj_get(module_value,"availability")!=NULL;if(!json_object_keys(module_value,has_availability?available_keys:module_keys,has_availability?8u:7u)||!yyjson_is_str(yyjson_obj_get(module_value,"kind"))||strcmp(yyjson_get_str(yyjson_obj_get(module_value,"kind")),"Module")!=0||!yyjson_is_str(yyjson_obj_get(module_value,"name"))||!yyjson_is_arr(yyjson_obj_get(module_value,"imports"))||!yyjson_is_arr(yyjson_obj_get(module_value,"types"))||!yyjson_is_arr(yyjson_obj_get(module_value,"functions"))||!yyjson_is_arr(yyjson_obj_get(module_value,"values"))){yyjson_doc_free(d);return sloph_syntax_diagnostic(c,"syntax.json.invalid","json","invalid Module node shape",SLOPH_UNKNOWN_SPAN,SLOPH_STATUS_INVALID_ARGUMENT);}}status=sloph_syntax_new_module(c,v,out);if(status==SLOPH_STATUS_OK)status=decode_module_json(*out,module_value);yyjson_doc_free(d);if(status==SLOPH_STATUS_OK)status=sloph_syntax_validate(c,*out);if(status!=SLOPH_STATUS_OK&&*out!=NULL){sloph_syntax_module_free(*out);*out=NULL;}return status;}
