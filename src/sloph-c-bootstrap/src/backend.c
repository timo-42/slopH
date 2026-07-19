#include "sloph/backend.h"

#include "core_internal.h"
#include "sloph/context.h"

#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *const sloph_timber_runtime[] = {
#include "backend_runtime.inc"
};

typedef struct {
    char *data;
    size_t length;
    size_t capacity;
    size_t limit;
    bool limit_exceeded;
    bool out_of_memory;
} Text;

typedef struct {
    SlophCoreDefinition *definition;
    SlophCoreBinder **parameters;
    size_t parameter_count;
    SlophCoreExpr *body;
} Function;

typedef struct {
    SlophCoreExpr *expression;
    size_t identity;
    const char **captures;
    size_t capture_count;
} Lambda;

typedef struct {
    const char *name;
    const char *value;
} Local;

typedef struct {
    SlophContext *context;
    SlophCoreUnit *unit;
    Text output;
    SlophCoreDefinition **definitions;
    Function *functions;
    size_t function_count;
    const char **constructors;
    size_t constructor_count;
    Lambda *lambdas;
    size_t lambda_count;
    size_t lambda_capacity;
    size_t temporary;
} Emitter;

static int compare_definitions(const void *left, const void *right) {
    const SlophCoreDefinition *const *a = left;
    const SlophCoreDefinition *const *b = right;
    return strcmp((*a)->name, (*b)->name);
}

static int compare_names(const void *left, const void *right) {
    const char *const *a = left;
    const char *const *b = right;
    return strcmp(*a, *b);
}

static bool text_reserve(Text *text, size_t additional) {
    size_t required, capacity;
    char *grown;
    if (additional > SIZE_MAX - text->length - 1u) {
        text->limit_exceeded = true;
        return false;
    }
    required = text->length + additional + 1u;
    if (required > text->limit) {
        text->limit_exceeded = true;
        return false;
    }
    if (required <= text->capacity) return true;
    capacity = text->capacity != 0u ? text->capacity : 4096u;
    while (capacity < required) {
        if (capacity > text->limit / 2u) { capacity = text->limit; break; }
        capacity *= 2u;
    }
    grown = realloc(text->data, capacity);
    if (grown == NULL) {
        text->out_of_memory = true;
        return false;
    }
    text->data = grown;
    text->capacity = capacity;
    return true;
}

static bool text_bytes(Text *text, const char *value, size_t length) {
    if (!text_reserve(text, length)) return false;
    memcpy(text->data + text->length, value, length);
    text->length += length;
    text->data[text->length] = '\0';
    return true;
}

static bool text_puts(Text *text, const char *value) {
    return text_bytes(text, value, strlen(value));
}

static bool text_printf(Text *text, const char *format, ...) {
    va_list arguments, copy;
    int length;
    va_start(arguments, format);
    va_copy(copy, arguments);
    length = vsnprintf(NULL, 0u, format, copy);
    va_end(copy);
    if (length < 0 || !text_reserve(text, (size_t)length)) {
        va_end(arguments);
        return false;
    }
    (void)vsnprintf(text->data + text->length, (size_t)length + 1u,
                    format, arguments);
    va_end(arguments);
    text->length += (size_t)length;
    return true;
}

static SlophStatus backend_failure(Emitter *emitter, const char *code,
                                   const char *message, SlophCoreSpan span) {
    SlophSpan public_span = {span.start, span.end};
    SlophStatus status = sloph_context_add_diagnostic(
        emitter->context, code, "backend", message, public_span);
    return status == SLOPH_STATUS_OK ? SLOPH_STATUS_INVALID_ARGUMENT : status;
}

static SlophStatus higher_order_failure(Emitter *emitter, const char *role,
                                        SlophCoreSpan span) {
    char message[160];
    char details[96];
    SlophSpan public_span = {span.start, span.end};
    SlophStatus status;
    (void)snprintf(message, sizeof(message),
        "the C11 first-order profile rejects function-typed %s", role);
    (void)snprintf(details, sizeof(details), "{\"role\":\"%s\"}", role);
    status = sloph_context_add_diagnostic_full(
        emitter->context, "backend.c11.higher_order_type", "backend",
        message, details, public_span, SLOPH_SEVERITY_ERROR);
    return status == SLOPH_STATUS_OK ? SLOPH_STATUS_INVALID_ARGUMENT : status;
}

static bool c_identifier(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    if (cursor == NULL || !(('A' <= *cursor && *cursor <= 'Z') ||
                            ('a' <= *cursor && *cursor <= 'z') ||
                            *cursor == '_')) return false;
    while (*++cursor != 0u)
        if (!(('A' <= *cursor && *cursor <= 'Z') ||
              ('a' <= *cursor && *cursor <= 'z') ||
              ('0' <= *cursor && *cursor <= '9') || *cursor == '_')) return false;
    return true;
}

static bool safe_header_name(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    if (cursor == NULL || *cursor == 0u || strstr(text, "..") != NULL) return false;
    for (; *cursor != 0u; ++cursor)
        if (!(('A' <= *cursor && *cursor <= 'Z') ||
              ('a' <= *cursor && *cursor <= 'z') ||
              ('0' <= *cursor && *cursor <= '9') || *cursor == '_' ||
              *cursor == '-' || *cursor == '.')) return false;
    return true;
}

static bool type_is(const SlophCoreType *type, SlophCoreTypeKind kind,
                    const char *name) {
    if (type == NULL || type->kind != kind) return false;
    return name == NULL || strcmp(type->as.name, name) == 0;
}

static bool result_shape(const SlophCoreType *type, const char **error_type) {
    if (type == NULL || type->kind != SLOPH_TYPE_APPLIED ||
        strcmp(type->as.applied.constructor, "sloph::Result") != 0 ||
        type->as.applied.count != 2u ||
        !type_is(type->as.applied.items[0], SLOPH_TYPE_INT, NULL) ||
        !type_is(type->as.applied.items[1], SLOPH_TYPE_NAMED, NULL)) return false;
    if (error_type != NULL) *error_type = type->as.applied.items[1]->as.name;
    return true;
}

static SlophStatus validate_foreign_bindings(Emitter *emitter) {
    size_t index, previous;
    for (index = 0u; index < emitter->unit->foreign_binding_count; ++index) {
        SlophCoreForeignBinding *binding = &emitter->unit->foreign_bindings[index];
        bool shape = false;
        for (previous = 0u; previous < index; ++previous)
            if (strcmp(binding->identity,
                       emitter->unit->foreign_bindings[previous].identity) == 0)
                return backend_failure(emitter, "backend.c11.foreign_identity",
                    "foreign binding identities must be unique", emitter->unit->span);
        if (!c_identifier(binding->symbol) || !safe_header_name(binding->header))
            return backend_failure(emitter, "backend.c11.foreign_metadata",
                "foreign binding symbol or header is unsafe for C11 emission",
                emitter->unit->span);
        if (strcmp(binding->adapter, "unavailable") == 0) continue;
        if (strcmp(binding->adapter, "borrowed_bytes_write") == 0) {
            shape = binding->parameter_count == 4u &&
                type_is(binding->parameters[0], SLOPH_TYPE_INT, NULL) &&
                type_is(binding->parameters[1], SLOPH_TYPE_BYTES, NULL) &&
                type_is(binding->parameters[2], SLOPH_TYPE_INT, NULL) &&
                type_is(binding->parameters[3], SLOPH_TYPE_INT, NULL) &&
                result_shape(binding->result, NULL);
        }
        if (!shape)
            return backend_failure(emitter, "backend.c11.foreign_adapter",
                "foreign binding does not match a supported C11 adapter contract",
                emitter->unit->span);
    }
    return SLOPH_STATUS_OK;
}

static size_t function_arity(const SlophCoreType *type) {
    size_t count = 0u;
    while (type->kind == SLOPH_TYPE_FORALL) type = type->as.forall.body;
    while (type->kind == SLOPH_TYPE_FUNCTION) {
        ++count;
        type = type->as.function.result;
    }
    return count;
}

static size_t definition_id(const Emitter *emitter, const char *name) {
    size_t index;
    for (index = 0u; index < emitter->unit->definition_count; ++index)
        if (strcmp(emitter->definitions[index]->name, name) == 0) return index;
    return SIZE_MAX;
}

static Function *find_function(const Emitter *emitter, const char *name) {
    size_t index;
    for (index = 0u; index < emitter->function_count; ++index)
        if (strcmp(emitter->functions[index].definition->name, name) == 0)
            return &emitter->functions[index];
    return NULL;
}

static size_t constructor_id(const Emitter *emitter, const char *name) {
    size_t index;
    for (index = 0u; index < emitter->constructor_count; ++index)
        if (strcmp(emitter->constructors[index], name) == 0) return index;
    return SIZE_MAX;
}

static char *qualified_constructor(const char *type, const char *name) {
    size_t type_length = strlen(type), name_length = strlen(name);
    char *result;
    if (type_length > SIZE_MAX - name_length - 3u) return NULL;
    result = malloc(type_length + name_length + 3u);
    if (result == NULL) return NULL;
    memcpy(result, type, type_length);
    result[type_length] = ':'; result[type_length + 1u] = ':';
    memcpy(result + type_length + 2u, name, name_length + 1u);
    return result;
}

static SlophCoreForeignBinding *find_binding(const Emitter *emitter,
                                             const char *identity) {
    size_t index;
    for (index = 0u; index < emitter->unit->foreign_binding_count; ++index)
        if (strcmp(emitter->unit->foreign_bindings[index].identity, identity) == 0)
            return &emitter->unit->foreign_bindings[index];
    return NULL;
}

static void expression_children(SlophCoreExpr *expression,
                                SlophCoreExpr ***items, size_t *count);

static SlophStatus check_v0_expression(Emitter *emitter,
                                       SlophCoreExpr *expression,
                                       bool top_level_body) {
    size_t index;
    if (expression->kind == SLOPH_EXPR_LAM) {
        if (!expression->as.lam.binder.is_type && !top_level_body)
            return backend_failure(emitter, "backend.c11.nested_lambda",
                "the C11 first-order profile rejects nested function values",
                expression->span);
        return check_v0_expression(emitter, expression->as.lam.body,
                                   top_level_body);
    }
    if (expression->kind == SLOPH_EXPR_GLOBAL &&
        find_function(emitter, expression->as.name) != NULL)
        return backend_failure(emitter, "backend.c11.function_escape",
            "top-level functions may only appear as direct saturated call targets",
            expression->span);
    if (expression->kind == SLOPH_EXPR_APP) {
        SlophCoreExpr *target = expression;
        size_t count = 0u;
        Function *function;
        while (target->kind == SLOPH_EXPR_APP) {
            SlophStatus status = check_v0_expression(emitter,
                target->as.app.argument, false);
            if (status != SLOPH_STATUS_OK) return status;
            ++count; target = target->as.app.function;
        }
        if (target->kind != SLOPH_EXPR_GLOBAL ||
            (function = find_function(emitter, target->as.name)) == NULL)
            return backend_failure(emitter, "backend.c11.dynamic_call",
                "calls must directly target a top-level function",
                expression->as.app.function->span);
        if (count != function->parameter_count)
            return backend_failure(emitter,
                count < function->parameter_count ? "backend.c11.partial_call" :
                                                     "backend.c11.oversaturated_call",
                "a first-order call must supply exactly the function arity",
                expression->span);
        return SLOPH_STATUS_OK;
    }
    if (expression->kind == SLOPH_EXPR_LET) {
        SlophStatus status;
        if (expression->as.let.binder.type->kind == SLOPH_TYPE_FUNCTION)
            return higher_order_failure(emitter, "let binding",
                                        expression->as.let.binder.span);
        status = check_v0_expression(emitter, expression->as.let.value, false);
        return status == SLOPH_STATUS_OK
            ? check_v0_expression(emitter, expression->as.let.body, false)
            : status;
    }
    if (expression->kind == SLOPH_EXPR_CASE) {
        SlophStatus status;
        if (expression->as.case_.result_type->kind == SLOPH_TYPE_FUNCTION)
            return higher_order_failure(emitter, "case result", expression->span);
        status = check_v0_expression(emitter,
            expression->as.case_.scrutinee, false);
        if (status != SLOPH_STATUS_OK) return status;
        for (index = 0u; index < expression->as.case_.alternative_count; ++index) {
            size_t binder;
            for (binder = 0u;
                 binder < expression->as.case_.alternatives[index].binder_count;
                 ++binder)
                if (expression->as.case_.alternatives[index].binders[binder].type->kind ==
                    SLOPH_TYPE_FUNCTION)
                    return higher_order_failure(emitter, "case binder",
                        expression->as.case_.alternatives[index].binders[binder].span);
            status = check_v0_expression(emitter,
                expression->as.case_.alternatives[index].body, false);
            if (status != SLOPH_STATUS_OK) return status;
        }
        return SLOPH_STATUS_OK;
    }
    {
        SlophCoreExpr **children;
        size_t count;
        expression_children(expression, &children, &count);
        for (index = 0u; index < count; ++index) {
            SlophStatus status = check_v0_expression(emitter, children[index], false);
            if (status != SLOPH_STATUS_OK) return status;
        }
    }
    return SLOPH_STATUS_OK;
}

static void expression_children(SlophCoreExpr *expression,
                                SlophCoreExpr ***items, size_t *count) {
    *items = NULL; *count = 0u;
    switch (expression->kind) {
        case SLOPH_EXPR_LAM: *items = &expression->as.lam.body; *count = 1u; break;
        case SLOPH_EXPR_APP: *items = &expression->as.app.function; *count = 2u; break;
        case SLOPH_EXPR_LET: *items = &expression->as.let.value; *count = 2u; break;
        case SLOPH_EXPR_PRIM: *items = expression->as.prim.items; *count = expression->as.prim.count; break;
        case SLOPH_EXPR_CON: *items = expression->as.con.fields; *count = expression->as.con.field_count; break;
        default: break;
    }
}

static bool name_present(const char **names, size_t count, const char *name) {
    size_t index;
    for (index = 0u; index < count; ++index)
        if (strcmp(names[index], name) == 0) return true;
    return false;
}

static bool collect_free(SlophCoreExpr *expression, const char **bound,
                         size_t bound_count, const char ***names,
                         size_t *count, size_t *capacity) {
    size_t index;
    if (expression->kind == SLOPH_EXPR_LOCAL) {
        if (!name_present(bound, bound_count, expression->as.name) &&
            !name_present(*names, *count, expression->as.name)) {
            const char **grown;
            if (*count == *capacity) {
                size_t next = *capacity != 0u ? *capacity * 2u : 4u;
                grown = realloc((void *)*names, next * sizeof(**names));
                if (grown == NULL) return false;
                *names = grown; *capacity = next;
            }
            (*names)[(*count)++] = expression->as.name;
        }
        return true;
    }
    if (expression->kind == SLOPH_EXPR_LAM && !expression->as.lam.binder.is_type) {
        const char **nested = malloc((bound_count + 1u) * sizeof(*nested));
        bool ok;
        if (nested == NULL) return false;
        if (bound_count != 0u) memcpy(nested, bound, bound_count * sizeof(*nested));
        nested[bound_count] = expression->as.lam.binder.name;
        ok = collect_free(expression->as.lam.body, nested, bound_count + 1u,
                          names, count, capacity);
        free(nested); return ok;
    }
    if (expression->kind == SLOPH_EXPR_LET) {
        const char **nested;
        bool ok = collect_free(expression->as.let.value, bound, bound_count,
                               names, count, capacity);
        if (!ok) return false;
        nested = malloc((bound_count + 1u) * sizeof(*nested));
        if (nested == NULL) return false;
        if (bound_count != 0u) memcpy(nested, bound, bound_count * sizeof(*nested));
        nested[bound_count] = expression->as.let.binder.name;
        ok = collect_free(expression->as.let.body, nested, bound_count + 1u,
                          names, count, capacity);
        free(nested); return ok;
    }
    if (expression->kind == SLOPH_EXPR_CASE) {
        if (!collect_free(expression->as.case_.scrutinee, bound, bound_count,
                          names, count, capacity)) return false;
        for (index = 0u; index < expression->as.case_.alternative_count; ++index) {
            SlophCoreAlternative *alternative = &expression->as.case_.alternatives[index];
            size_t nested_count = bound_count + alternative->binder_count;
            const char **nested = malloc((nested_count != 0u ? nested_count : 1u) * sizeof(*nested));
            bool ok;
            size_t binder;
            if (nested == NULL) return false;
            if (bound_count != 0u) memcpy(nested, bound, bound_count * sizeof(*nested));
            for (binder = 0u; binder < alternative->binder_count; ++binder)
                nested[bound_count + binder] = alternative->binders[binder].name;
            ok = collect_free(alternative->body, nested, nested_count,
                              names, count, capacity);
            free(nested);
            if (!ok) return false;
        }
        return true;
    }
    {
        SlophCoreExpr **children;
        size_t child_count;
        expression_children(expression, &children, &child_count);
        for (index = 0u; index < child_count; ++index)
            if (!collect_free(children[index], bound, bound_count,
                              names, count, capacity)) return false;
    }
    return true;
}

static bool collect_lambdas(Emitter *emitter, SlophCoreExpr *expression) {
    size_t index;
    if (expression->kind == SLOPH_EXPR_LAM && !expression->as.lam.binder.is_type) {
        Lambda *lambda;
        const char *bound[1];
        size_t capture_capacity = 0u;
        if (emitter->lambda_count == emitter->lambda_capacity) {
            size_t next = emitter->lambda_capacity != 0u ? emitter->lambda_capacity * 2u : 8u;
            Lambda *grown = realloc(emitter->lambdas, next * sizeof(*grown));
            if (grown == NULL) return false;
            emitter->lambdas = grown; emitter->lambda_capacity = next;
        }
        lambda = &emitter->lambdas[emitter->lambda_count++];
        memset(lambda, 0, sizeof(*lambda));
        lambda->expression = expression;
        lambda->identity = emitter->unit->definition_count + emitter->lambda_count - 1u;
        bound[0] = expression->as.lam.binder.name;
        if (!collect_free(expression->as.lam.body, bound, 1u,
                          &lambda->captures, &lambda->capture_count,
                          &capture_capacity)) return false;
        qsort((void *)lambda->captures, lambda->capture_count,
              sizeof(*lambda->captures), compare_names);
    }
    if (expression->kind == SLOPH_EXPR_CASE) {
        if (!collect_lambdas(emitter, expression->as.case_.scrutinee)) return false;
        for (index = 0u; index < expression->as.case_.alternative_count; ++index)
            if (!collect_lambdas(emitter, expression->as.case_.alternatives[index].body)) return false;
        return true;
    }
    {
        SlophCoreExpr **children;
        size_t count;
        expression_children(expression, &children, &count);
        for (index = 0u; index < count; ++index)
            if (!collect_lambdas(emitter, children[index])) return false;
    }
    return true;
}

static Lambda *find_lambda(Emitter *emitter, SlophCoreExpr *expression) {
    size_t index;
    for (index = 0u; index < emitter->lambda_count; ++index)
        if (emitter->lambdas[index].expression == expression) return &emitter->lambdas[index];
    return NULL;
}

static const char *local_value(const Local *locals, size_t count,
                               const char *name) {
    while (count != 0u) {
        --count;
        if (strcmp(locals[count].name, name) == 0) return locals[count].value;
    }
    return NULL;
}

static size_t new_temporary(Emitter *emitter) { return emitter->temporary++; }

static bool c_string(Text *output, const char *value) {
    const unsigned char *cursor = (const unsigned char *)value;
    if (!text_puts(output, "\"")) return false;
    while (*cursor != 0u) {
        unsigned char byte = *cursor++;
        if (byte == '\\' || byte == '"') {
            if (!text_printf(output, "\\%c", (int)byte)) return false;
        } else if (byte >= 32u && byte <= 126u) {
            if (!text_bytes(output, (const char *)&byte, 1u)) return false;
        } else if (!text_printf(output, "\\%03o", (unsigned)byte)) return false;
    }
    return text_puts(output, "\"");
}

static bool emit_expression(Emitter *, SlophCoreExpr *, const Local *, size_t,
                            const char *, size_t *);

static bool emit_call(Emitter *emitter, const char *name, size_t result,
                      const size_t *values, size_t count, const char *indent) {
    size_t index;
    if (!text_printf(&emitter->output, "%sSlValue *t%zu=sl_f%zu(", indent,
                     result, definition_id(emitter, name))) return false;
    for (index = 0u; index < count; ++index)
        if (!text_printf(&emitter->output, "%st%zu", index != 0u ? ", " : "",
                         values[index])) return false;
    return text_puts(&emitter->output, ");\n");
}

static bool emit_primitive(Emitter *emitter, SlophCoreExpr *expression,
                           const Local *locals, size_t local_count,
                           const char *indent, size_t *out_result) {
    size_t *values, index, result;
    const char *name = expression->as.prim.name;
    SlophCoreForeignBinding *binding;
    values = calloc(expression->as.prim.count != 0u ? expression->as.prim.count : 1u,
                    sizeof(*values));
    if (values == NULL) return false;
    for (index = 0u; index < expression->as.prim.count; ++index)
        if (!emit_expression(emitter, expression->as.prim.items[index], locals,
                             local_count, indent, &values[index])) { free(values); return false; }
    result = new_temporary(emitter);
    if (strcmp(name, "bytes.length") == 0) {
        if (!text_printf(&emitter->output,
            "%sif(t%zu->kind!=2u)sl_die(\"bytes.length received non-Bytes value\");\n"
            "%sSlValue *t%zu=sl_int_u64((uint64_t)t%zu->as.bytes.len);\n",
            indent, values[0], indent, result, values[0])) goto failed;
    } else if (strcmp(name, "int.to_bytes") == 0) {
        if (!text_printf(&emitter->output, "%sSlValue *t%zu=sl_int_to_bytes(t%zu);\n",
                         indent, result, values[0])) goto failed;
    } else if (strcmp(name, "runtime.trap") == 0) {
        if (!text_printf(&emitter->output, "%ssl_trap_bytes(t%zu);\n%sSlValue *t%zu=NULL;\n",
                         indent, values[0], indent, result)) goto failed;
    } else if (strcmp(name, "memory.allocate") == 0) {
        size_t ok=constructor_id(emitter,"sloph::Result::Ok"),err=constructor_id(emitter,"sloph::Result::Err");
        size_t invalid=constructor_id(emitter,"core::memory::AllocationError::InvalidSize");
        size_t limit=constructor_id(emitter,"core::memory::AllocationError::LimitExceeded");
        size_t oom_tag=constructor_id(emitter,"core::memory::AllocationError::OutOfMemory");
        emitter->temporary += 6u;
        if (!text_printf(&emitter->output,
            "%ssize_t t%zu=0u;SlValue *t%zu=NULL;\n"
            "%sif(!sl_int_size(t%zu,&t%zu)){SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}\n"
            "%selse if(t%zu>SL_MAX_BLOCK_BYTES-sl_active_block_bytes){SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}\n"
            "%selse{SlValue *t%zu=sl_block_new(t%zu);if(t%zu!=NULL){SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}else{SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}}\n",
            indent,result+1u,result,
            indent,values[0],result+1u,result+2u,invalid,result+3u,result+2u,result,err,result+3u,
            indent,result+1u,result+2u,limit,result+3u,result+2u,result,err,result+3u,
            indent,result+2u,result+1u,result+2u,result+3u,result+2u,result,ok,result+3u,
            result+4u,oom_tag,result+5u,result+4u,result,err,result+5u)) goto failed;
    } else if (strcmp(name, "memory.capacity") == 0) {
        if (!text_printf(&emitter->output,
            "%sSlBlock *t%zu=sl_block_get(t%zu);SlValue *t%zu=sl_int_u64((uint64_t)t%zu->capacity);\n",
            indent,result+1u,values[0],result,result+1u)) goto failed;
        emitter->temporary += 1u;
    } else if (strcmp(name, "memory.read") == 0) {
        size_t ok=constructor_id(emitter,"sloph::Result::Ok"),err=constructor_id(emitter,"sloph::Result::Err");
        size_t bounds=constructor_id(emitter,"core::memory::AccessError::OutOfBounds");
        emitter->temporary += 5u;
        if (!text_printf(&emitter->output,
            "%sSlBlock *t%zu=sl_block_get(t%zu);size_t t%zu=0u;SlValue *t%zu=NULL;\n"
            "%sif(!sl_int_size(t%zu,&t%zu)||t%zu>=t%zu->capacity){SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}\n"
            "%selse{SlValue *t%zu[]={sl_int_u64((uint64_t)t%zu->data[t%zu])};t%zu=sl_con(%zuu,1u,t%zu);}\n",
            indent,result+1u,values[0],result+2u,result,
            indent,values[1],result+2u,result+2u,result+1u,result+3u,bounds,result+4u,result+3u,result,err,result+4u,
            indent,result+3u,result+1u,result+2u,result,ok,result+3u)) goto failed;
    } else if (strcmp(name, "memory.write") == 0) {
        size_t ok=constructor_id(emitter,"sloph::Result::Ok"),err=constructor_id(emitter,"sloph::Result::Err");
        size_t bounds=constructor_id(emitter,"core::memory::AccessError::OutOfBounds");
        size_t invalid=constructor_id(emitter,"core::memory::AccessError::InvalidByte");
        size_t unit=constructor_id(emitter,"sloph::Unit::Unit");
        emitter->temporary += 7u;
        if (!text_printf(&emitter->output,
            "%sSlBlock *t%zu=sl_block_get(t%zu);size_t t%zu=0u,t%zu=0u;SlValue *t%zu=NULL;\n"
            "%sif(!sl_int_size(t%zu,&t%zu)||t%zu>=t%zu->capacity){SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}\n"
            "%selse if(!sl_int_size(t%zu,&t%zu)||t%zu>255u){SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}\n"
            "%selse{t%zu->data[t%zu]=(unsigned char)t%zu;SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}\n",
            indent,result+1u,values[0],result+2u,result+3u,result,
            indent,values[1],result+2u,result+2u,result+1u,result+4u,bounds,result+5u,result+4u,result,err,result+5u,
            indent,values[2],result+3u,result+3u,result+4u,invalid,result+5u,result+4u,result,err,result+5u,
            indent,result+1u,result+2u,result+3u,result+4u,unit,result+5u,result+4u,result,ok,result+5u)) goto failed;
    } else if (strcmp(name, "memory.copy") == 0) {
        size_t ok=constructor_id(emitter,"sloph::Result::Ok"),err=constructor_id(emitter,"sloph::Result::Err");
        size_t bounds=constructor_id(emitter,"core::memory::AccessError::OutOfBounds");
        size_t unit=constructor_id(emitter,"sloph::Unit::Unit");
        emitter->temporary += 9u;
        if (!text_printf(&emitter->output,
            "%sSlBlock *t%zu=sl_block_get(t%zu),*t%zu=sl_block_get(t%zu);size_t t%zu=0u,t%zu=0u,t%zu=0u;SlValue *t%zu=NULL;\n"
            "%sif(t%zu==t%zu)sl_die(\"memory.copy source and target must be distinct Blocks\");\n"
            "%sif(!sl_int_size(t%zu,&t%zu)||!sl_int_size(t%zu,&t%zu)||!sl_int_size(t%zu,&t%zu)||t%zu>t%zu->capacity||t%zu>t%zu->capacity-t%zu||t%zu>t%zu->capacity||t%zu>t%zu->capacity-t%zu){SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}\n"
            "%selse{if(t%zu)memcpy(t%zu->data+t%zu,t%zu->data+t%zu,t%zu);SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}\n",
            indent,result+1u,values[0],result+2u,values[2],result+3u,result+4u,result+5u,result,
            indent,result+1u,result+2u,
            indent,values[1],result+3u,values[3],result+4u,values[4],result+5u,result+3u,result+1u,result+5u,result+1u,result+3u,result+4u,result+2u,result+5u,result+2u,result+4u,result+6u,bounds,result+7u,result+6u,result,err,result+7u,
            indent,result+5u,result+2u,result+4u,result+1u,result+3u,result+5u,result+6u,unit,result+7u,result+6u,result,ok,result+7u)) goto failed;
    } else if (strcmp(name, "memory.release") == 0) {
        size_t unit=constructor_id(emitter,"sloph::Unit::Unit");
        if (!text_printf(&emitter->output,
            "%ssl_block_release(t%zu);SlValue *t%zu=sl_con(%zuu,0u,NULL);\n",
            indent,values[0],result,unit)) goto failed;
    } else if (strcmp(name, "int.add") == 0 || strcmp(name, "int.sub") == 0 ||
               strcmp(name, "int.mul") == 0) {
        const char *operation = strcmp(name, "int.add") == 0 ? "add" :
                                strcmp(name, "int.sub") == 0 ? "sub" : "mul";
        if (!text_printf(&emitter->output, "%sSlValue *t%zu=sl_int_%s(t%zu,t%zu);\n",
                         indent, result, operation, values[0], values[1])) goto failed;
    } else if (strcmp(name, "int.equal") == 0 || strcmp(name, "int.less") == 0) {
        size_t false_tag = constructor_id(emitter, "sloph::Bool::False");
        size_t true_tag = constructor_id(emitter, "sloph::Bool::True");
        const char *operation = strcmp(name, "int.equal") == 0 ? "==0" : "<0";
        if (!text_printf(&emitter->output,
            "%sSlValue *t%zu=sl_con(sl_int_compare(t%zu,t%zu)%s?%zuu:%zuu,0u,NULL);\n",
            indent, result, values[0], values[1], operation, true_tag, false_tag)) goto failed;
    } else if ((binding = find_binding(emitter, name)) != NULL) {
        size_t ok = constructor_id(emitter, "sloph::Result::Ok");
        size_t err = constructor_id(emitter, "sloph::Result::Err");
        if (strcmp(binding->adapter, "borrowed_bytes_write") == 0) {
            const char *error_type = binding->result->as.applied.items[1]->as.name;
            char *interrupted = qualified_constructor(error_type, "Interrupted");
            char *native_error = qualified_constructor(error_type, "Native");
            if (interrupted == NULL || native_error == NULL) {
                free(interrupted); free(native_error); goto failed;
            }
            if (!text_printf(&emitter->output,
                "%suint64_t t%zu=sl_int_u64_value(t%zu,\"file descriptor is outside C int range\");\n"
                "%sif(t%zu>(uint64_t)INT_MAX)sl_die(\"file descriptor is outside C int range\");\n"
                "%sif(t%zu->kind!=2u)sl_die(\"foreign write received non-Bytes value\");\n"
                "%suint64_t t%zu=sl_int_u64_value(t%zu,\"write offset is outside native range\");\n"
                "%suint64_t t%zu=sl_int_u64_value(t%zu,\"write count is outside native range\");\n"
                "%sif(t%zu>(uint64_t)t%zu->as.bytes.len||t%zu>(uint64_t)t%zu->as.bytes.len-t%zu||t%zu>(uint64_t)(SIZE_MAX>>1u))sl_die(\"foreign write range is invalid\");\n",
                indent,result+1u,values[0],indent,result+1u,indent,values[1],indent,result+2u,values[2],indent,result+3u,values[3],
                indent,result+2u,values[1],result+3u,values[1],result+2u,result+3u)) goto failed;
            emitter->temporary += 9u;
            if (!text_printf(&emitter->output,
                "%serrno=0;ssize_t t%zu=%s((int)t%zu,t%zu->as.bytes.len?t%zu->as.bytes.data+(size_t)t%zu:NULL,(size_t)t%zu);int t%zu=errno;\n"
                "%sSlValue *t%zu=NULL;if(t%zu>=0){SlValue *t%zu[]={sl_int_u64((uint64_t)t%zu)};t%zu=sl_con(%zuu,1u,t%zu);}"
                "else if(t%zu==EINTR){SlValue *t%zu=sl_con(%zuu,0u,NULL);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}"
                "else{SlValue *t%zu[]={sl_int_u64((uint64_t)(unsigned)t%zu)};SlValue *t%zu=sl_con(%zuu,1u,t%zu);SlValue *t%zu[]={t%zu};t%zu=sl_con(%zuu,1u,t%zu);}\n",
                indent,result+4u,binding->symbol,result+1u,values[1],values[1],result+2u,result+3u,result+5u,
                indent,result,result+4u,result+6u,result+4u,result,ok,result+6u,
                result+5u,result+7u,constructor_id(emitter,interrupted),result+8u,result+7u,result,err,result+8u,
                result+6u,result+5u,result+7u,constructor_id(emitter,native_error),result+6u,result+8u,result+7u,result,err,result+8u)) {
                free(interrupted); free(native_error); goto failed;
            }
            free(interrupted); free(native_error);
        } else goto failed;
    } else goto failed;
    free(values); *out_result = result; return true;
failed:
    free(values); return false;
}

static bool emit_expression(Emitter *emitter, SlophCoreExpr *expression,
                            const Local *locals, size_t local_count,
                            const char *indent, size_t *out_result) {
    size_t result, index;
    switch (expression->kind) {
        case SLOPH_EXPR_INT:
            result = new_temporary(emitter);
            if (!text_printf(&emitter->output, "%sSlValue *t%zu=sl_int_literal(", indent, result) ||
                !c_string(&emitter->output, expression->as.integer) ||
                !text_puts(&emitter->output, ");\n")) return false;
            *out_result = result; return true;
        case SLOPH_EXPR_BYTES:
            result = new_temporary(emitter);
            if (expression->as.bytes.length == 0u) {
                if (!text_printf(&emitter->output, "%sSlValue *t%zu=sl_bytes(NULL,0u);\n",
                                 indent, result)) return false;
            } else {
                size_t array = new_temporary(emitter);
                if (!text_printf(&emitter->output, "%sconst unsigned char t%zu[]={", indent, array)) return false;
                for (index = 0u; index < expression->as.bytes.length; ++index)
                    if (!text_printf(&emitter->output, "%s0x%02xu",
                        index != 0u ? "," : "", (unsigned)expression->as.bytes.data[index])) return false;
                if (!text_printf(&emitter->output, "};\n%sSlValue *t%zu=sl_bytes(t%zu,%zuu);\n",
                                 indent, result, array, expression->as.bytes.length)) return false;
            }
            *out_result = result; return true;
        case SLOPH_EXPR_LOCAL: {
            const char *value = local_value(locals, local_count, expression->as.name);
            if (value == NULL || value[0] != 't') return false;
            *out_result = (size_t)strtoull(value + 1, NULL, 10); return true;
        }
        case SLOPH_EXPR_GLOBAL: {
            Function *function = find_function(emitter, expression->as.name);
            result = new_temporary(emitter);
            if (function != NULL) {
                if (!text_printf(&emitter->output,
                    "%sSlValue *t%zu=sl_closure(%zuu,%zuu,0u,NULL,0u,NULL);\n",
                    indent, result, definition_id(emitter, expression->as.name),
                    function->parameter_count)) return false;
            } else if (!text_printf(&emitter->output, "%sSlValue *t%zu=sl_g%zu();\n",
                                    indent, result, definition_id(emitter, expression->as.name))) return false;
            *out_result = result; return true;
        }
        case SLOPH_EXPR_TYPE: return false;
        case SLOPH_EXPR_APP:
            if (expression->as.app.argument->kind == SLOPH_EXPR_TYPE)
                return emit_expression(emitter, expression->as.app.function,
                                       locals, local_count, indent, out_result);
            if (emitter->unit->version == 0) {
                SlophCoreExpr *target = expression;
                SlophCoreExpr **arguments = NULL;
                size_t count = 0u, capacity = 0u;
                size_t *values;
                while (target->kind == SLOPH_EXPR_APP) {
                    if (count == capacity) {
                        size_t next = capacity != 0u ? capacity * 2u : 4u;
                        SlophCoreExpr **grown = realloc(arguments, next * sizeof(*grown));
                        if (grown == NULL) { free(arguments); return false; }
                        arguments = grown; capacity = next;
                    }
                    arguments[count++] = target->as.app.argument;
                    target = target->as.app.function;
                }
                if (target->kind != SLOPH_EXPR_GLOBAL) { free(arguments); return false; }
                values = calloc(count != 0u ? count : 1u, sizeof(*values));
                if (values == NULL) { free(arguments); return false; }
                for (index = 0u; index < count; ++index)
                    if (!emit_expression(emitter, arguments[count - index - 1u], locals,
                                         local_count, indent, &values[index])) {
                        free(values); free(arguments); return false;
                    }
                result = new_temporary(emitter);
                if (!emit_call(emitter, target->as.name, result, values, count, indent)) {
                    free(values); free(arguments); return false;
                }
                free(values); free(arguments); *out_result = result; return true;
            } else {
                size_t function, argument;
                if (!emit_expression(emitter, expression->as.app.function, locals,
                                     local_count, indent, &function) ||
                    !emit_expression(emitter, expression->as.app.argument, locals,
                                     local_count, indent, &argument)) return false;
                result = new_temporary(emitter);
                if (!text_printf(&emitter->output, "%sSlValue *t%zu=sl_apply(t%zu,t%zu);\n",
                                 indent, result, function, argument)) return false;
                *out_result = result; return true;
            }
        case SLOPH_EXPR_LAM:
            if (expression->as.lam.binder.is_type)
                return emit_expression(emitter, expression->as.lam.body, locals,
                                       local_count, indent, out_result);
            else {
                Lambda *lambda = find_lambda(emitter, expression);
                result = new_temporary(emitter);
                if (lambda == NULL) return false;
                if (lambda->capture_count == 0u) {
                    if (!text_printf(&emitter->output,
                        "%sSlValue *t%zu=sl_closure(%zuu,1u,0u,NULL,0u,NULL);\n",
                        indent, result, lambda->identity)) return false;
                } else {
                    size_t captured = new_temporary(emitter);
                    if (!text_printf(&emitter->output, "%sSlValue *t%zu[]={", indent, captured)) return false;
                    for (index = 0u; index < lambda->capture_count; ++index) {
                        const char *value = local_value(locals, local_count, lambda->captures[index]);
                        if (value == NULL || !text_printf(&emitter->output, "%s%s",
                            index != 0u ? "," : "", value)) return false;
                    }
                    if (!text_printf(&emitter->output,
                        "};\n%sSlValue *t%zu=sl_closure(%zuu,1u,0u,NULL,%zuu,t%zu);\n",
                        indent, result, lambda->identity, lambda->capture_count, captured)) return false;
                }
                *out_result = result; return true;
            }
        case SLOPH_EXPR_LET: {
            size_t value;
            Local *nested;
            char *name;
            if (!emit_expression(emitter, expression->as.let.value, locals,
                                 local_count, indent, &value) ||
                !text_printf(&emitter->output, "%s(void)t%zu;\n", indent, value)) return false;
            nested = malloc((local_count + 1u) * sizeof(*nested));
            name = malloc(32u);
            if (nested == NULL || name == NULL) { free(nested); free(name); return false; }
            if (local_count != 0u) memcpy(nested, locals, local_count * sizeof(*nested));
            (void)snprintf(name, 32u, "t%zu", value);
            nested[local_count].name = expression->as.let.binder.name;
            nested[local_count].value = name;
            {
                bool ok = emit_expression(emitter, expression->as.let.body, nested,
                                          local_count + 1u, indent, out_result);
                free(name); free(nested); return ok;
            }
        }
        case SLOPH_EXPR_PRIM:
            return emit_primitive(emitter, expression, locals, local_count,
                                  indent, out_result);
        case SLOPH_EXPR_CON: {
            size_t *values = calloc(expression->as.con.field_count != 0u ?
                                    expression->as.con.field_count : 1u,
                                    sizeof(*values));
            if (values == NULL) return false;
            for (index = 0u; index < expression->as.con.field_count; ++index)
                if (!emit_expression(emitter, expression->as.con.fields[index], locals,
                                     local_count, indent, &values[index])) { free(values); return false; }
            result = new_temporary(emitter);
            if (expression->as.con.field_count != 0u) {
                size_t array = new_temporary(emitter);
                if (!text_printf(&emitter->output, "%sSlValue *t%zu[]={", indent, array)) { free(values); return false; }
                for (index = 0u; index < expression->as.con.field_count; ++index)
                    if (!text_printf(&emitter->output, "%st%zu", index != 0u ? "," : "", values[index])) { free(values); return false; }
                if (!text_printf(&emitter->output, "};\n%sSlValue *t%zu=sl_con(%zuu,%zuu,t%zu);\n",
                    indent, result, constructor_id(emitter, expression->as.con.constructor),
                    expression->as.con.field_count, array)) { free(values); return false; }
            } else if (!text_printf(&emitter->output, "%sSlValue *t%zu=sl_con(%zuu,0u,NULL);\n",
                indent, result, constructor_id(emitter, expression->as.con.constructor))) { free(values); return false; }
            free(values); *out_result = result; return true;
        }
        case SLOPH_EXPR_CASE: {
            size_t scrutinee;
            if (!emit_expression(emitter, expression->as.case_.scrutinee, locals,
                                 local_count, indent, &scrutinee)) return false;
            result = new_temporary(emitter);
            if (!text_printf(&emitter->output,
                "%sif(t%zu->kind!=1u)sl_die(\"case received non-constructor value\");\n"
                "%sSlValue *t%zu=NULL;\n%sswitch(t%zu->as.con.tag){\n",
                indent,scrutinee,indent,result,indent,scrutinee)) return false;
            for (index = 0u; index < expression->as.case_.alternative_count; ++index) {
                SlophCoreAlternative *alternative = &expression->as.case_.alternatives[index];
                Local *nested = malloc((local_count + alternative->binder_count) * sizeof(*nested));
                char **names = calloc(alternative->binder_count != 0u ? alternative->binder_count : 1u,
                                      sizeof(*names));
                size_t binder, branch;
                if ((local_count + alternative->binder_count != 0u && nested == NULL) || names == NULL) {
                    free(nested); free(names); return false;
                }
                if (local_count != 0u) memcpy(nested, locals, local_count * sizeof(*nested));
                if (!text_printf(&emitter->output, "%scase %zuu:{\n", indent,
                                 constructor_id(emitter, alternative->constructor))) return false;
                for (binder = 0u; binder < alternative->binder_count; ++binder) {
                    size_t temporary = new_temporary(emitter);
                    names[binder] = malloc(32u);
                    if (names[binder] == NULL) return false;
                    (void)snprintf(names[binder], 32u, "t%zu", temporary);
                    nested[local_count + binder].name = alternative->binders[binder].name;
                    nested[local_count + binder].value = names[binder];
                    if (!text_printf(&emitter->output,
                        "%s  SlValue *t%zu=t%zu->as.con.field[%zuu];(void)t%zu;\n",
                        indent, temporary, scrutinee, binder, temporary)) return false;
                }
                if (!emit_expression(emitter, alternative->body, nested,
                                     local_count + alternative->binder_count,
                                     indent, &branch) ||
                    !text_printf(&emitter->output, "%s  t%zu=t%zu;break;}\n",
                                 indent, result, branch)) return false;
                for (binder = 0u; binder < alternative->binder_count; ++binder) free(names[binder]);
                free(names); free(nested);
            }
            if (!text_printf(&emitter->output,
                "%sdefault:sl_die(\"invalid case constructor\");}\n", indent)) return false;
            *out_result = result; return true;
        }
    }
    return false;
}

static bool emit_declarations(Emitter *emitter) {
    size_t index, parameter;
    for (index = 0u; index < emitter->function_count; ++index) {
        Function *function = &emitter->functions[index];
        if (!text_printf(&emitter->output, "static SlValue *sl_f%zu(",
                         definition_id(emitter, function->definition->name))) return false;
        if (function->parameter_count == 0u) {
            if (!text_puts(&emitter->output, "void")) return false;
        }
        for (parameter = 0u; parameter < function->parameter_count; ++parameter)
            if (!text_printf(&emitter->output, "%sSlValue *a%zu",
                parameter != 0u ? "," : "", parameter)) return false;
        if (!text_puts(&emitter->output, ");\n")) return false;
    }
    for (index = 0u; index < emitter->lambda_count; ++index)
        if (!text_printf(&emitter->output,
            "static SlValue *sl_l%zu(SlValue **environment,SlValue *argument);\n",
            emitter->lambdas[index].identity)) return false;
    for (index = 0u; index < emitter->unit->definition_count; ++index)
        if (find_function(emitter, emitter->definitions[index]->name) == NULL &&
            !text_printf(&emitter->output, "static SlValue *sl_g%zu(void);\n", index)) return false;
    return true;
}

static bool emit_dispatch(Emitter *emitter) {
    size_t index, parameter;
    if (!text_puts(&emitter->output,
        "static SlValue *sl_dispatch(uint32_t function,size_t count,SlValue **argument,size_t environment_count,SlValue **environment){\n"
        "(void)count;(void)argument;(void)environment_count;(void)environment;switch(function){\n")) return false;
    for (index = 0u; index < emitter->function_count; ++index) {
        Function *function = &emitter->functions[index];
        if (!text_printf(&emitter->output,
            "case %zuu:if(count!=%zuu||environment_count!=0u)sl_die(\"closure arity mismatch\");return sl_f%zu(",
            definition_id(emitter, function->definition->name), function->parameter_count,
            definition_id(emitter, function->definition->name))) return false;
        for (parameter = 0u; parameter < function->parameter_count; ++parameter)
            if (!text_printf(&emitter->output, "%sargument[%zuu]",
                parameter != 0u ? "," : "", parameter)) return false;
        if (!text_puts(&emitter->output, ");\n")) return false;
    }
    for (index = 0u; index < emitter->lambda_count; ++index)
        if (!text_printf(&emitter->output,
            "case %zuu:if(count!=1u||environment_count!=%zuu)sl_die(\"lambda closure mismatch\");return sl_l%zu(environment,argument[0]);\n",
            emitter->lambdas[index].identity, emitter->lambdas[index].capture_count,
            emitter->lambdas[index].identity)) return false;
    return text_puts(&emitter->output,
        "default:sl_die(\"unknown closure function\");}return NULL;}\n");
}

static bool emit_printer(Emitter *emitter) {
    size_t index;
    if (!text_puts(&emitter->output,
        "static void sl_print_node(SlValue *value){if(++sl_print_depth>SL_MAX_DEPTH)sl_die(\"print depth exceeds 4096\");"
        "if(value->kind==0u){sl_text(\"(int \" );sl_print_big(value->as.integer);sl_char(')');--sl_print_depth;return;}"
        "if(value->kind==2u){static const char h[]=\"0123456789abcdef\";sl_text(\"(bytes x\");for(size_t i=0u;i<value->as.bytes.len;++i){char b[2]={h[value->as.bytes.data[i]>>4u],h[value->as.bytes.data[i]&15u]};sl_write(b,2u);}sl_char(')');--sl_print_depth;return;}"
        "if(value->kind==3u)sl_die(\"function values are not printable\");switch(value->as.con.tag){")) return false;
    for (index = 0u; index < emitter->constructor_count; ++index) {
        if (!text_printf(&emitter->output, "case %zuu:sl_text(\"(con \" );sl_text(", index) ||
            !c_string(&emitter->output, emitter->constructors[index]) ||
            !text_puts(&emitter->output, ");break;")) return false;
    }
    return text_puts(&emitter->output,
        "default:sl_die(\"invalid constructor tag\");}for(size_t i=0u;i<value->as.con.count;++i){sl_char(' ');sl_print_node(value->as.con.field[i]);}sl_char(')');--sl_print_depth;}\n"
        "static void sl_print_value(SlValue *value){sl_text(\"(value 0 \" );sl_print_node(value);sl_char(')');}\n");
}

static bool emit_function(Emitter *emitter, Function *function) {
    Local *locals;
    char **names;
    size_t parameter, result;
    locals = calloc(function->parameter_count != 0u ? function->parameter_count : 1u,
                    sizeof(*locals));
    names = calloc(function->parameter_count != 0u ? function->parameter_count : 1u,
                   sizeof(*names));
    if (locals == NULL || names == NULL) { free(locals); free(names); return false; }
    if (!text_printf(&emitter->output, "static SlValue *sl_f%zu(",
                     definition_id(emitter, function->definition->name))) return false;
    for (parameter = 0u; parameter < function->parameter_count; ++parameter)
        if (!text_printf(&emitter->output, "%sSlValue *a%zu",
                         parameter != 0u ? "," : "", parameter)) return false;
    if (!text_puts(&emitter->output, "){sl_eval_enter();sl_charge(1u);\n")) return false;
    for (parameter = 0u; parameter < function->parameter_count; ++parameter) {
        names[parameter] = malloc(32u);
        if (names[parameter] == NULL) return false;
        (void)snprintf(names[parameter], 32u, "t%zu", emitter->temporary);
        if (!text_printf(&emitter->output, "SlValue *t%zu=a%zu;(void)t%zu;\n",
                         emitter->temporary, parameter, emitter->temporary)) return false;
        locals[parameter].name = function->parameters[parameter]->name;
        locals[parameter].value = names[parameter];
        ++emitter->temporary;
    }
    if (!emit_expression(emitter, function->body, locals,
                         function->parameter_count, "", &result) ||
        !text_printf(&emitter->output,
                     "sl_eval_leave();return t%zu;}\n", result)) return false;
    for (parameter = 0u; parameter < function->parameter_count; ++parameter) free(names[parameter]);
    free(names); free(locals); return true;
}

static bool emit_lambda(Emitter *emitter, Lambda *lambda) {
    Local *locals;
    char **names;
    size_t index, result;
    size_t count = lambda->capture_count + 1u;
    locals = calloc(count, sizeof(*locals));
    names = calloc(count, sizeof(*names));
    if (locals == NULL || names == NULL) { free(locals); free(names); return false; }
    if (!text_printf(&emitter->output,
        "static SlValue *sl_l%zu(SlValue **environment,SlValue *argument){sl_eval_enter();sl_charge(1u);(void)environment;(void)argument;\n",
        lambda->identity)) return false;
    for (index = 0u; index < lambda->capture_count; ++index) {
        names[index] = malloc(32u);
        if (names[index] == NULL) return false;
        (void)snprintf(names[index], 32u, "t%zu", emitter->temporary);
        if (!text_printf(&emitter->output, "SlValue *t%zu=environment[%zuu];(void)t%zu;\n",
                         emitter->temporary, index, emitter->temporary)) return false;
        locals[index].name = lambda->captures[index]; locals[index].value = names[index];
        ++emitter->temporary;
    }
    names[count - 1u] = malloc(32u);
    if (names[count - 1u] == NULL) return false;
    (void)snprintf(names[count - 1u], 32u, "t%zu", emitter->temporary);
    if (!text_printf(&emitter->output, "SlValue *t%zu=argument;(void)t%zu;\n",
                     emitter->temporary, emitter->temporary)) return false;
    locals[count - 1u].name = lambda->expression->as.lam.binder.name;
    locals[count - 1u].value = names[count - 1u];
    ++emitter->temporary;
    if (!emit_expression(emitter, lambda->expression->as.lam.body,
                         locals, count, "", &result) ||
        !text_printf(&emitter->output, "sl_eval_leave();return t%zu;}\n", result)) return false;
    for (index = 0u; index < count; ++index) free(names[index]);
    free(names); free(locals); return true;
}

static bool emit_global(Emitter *emitter, SlophCoreDefinition *definition) {
    size_t id = definition_id(emitter, definition->name), result;
    if (!text_printf(&emitter->output,
        "static SlValue *sl_g%zu_cache=NULL;static unsigned sl_g%zu_state=0u;\n"
        "static SlValue *sl_g%zu(void){if(sl_g%zu_state==2u)return sl_g%zu_cache;if(sl_g%zu_state==1u)sl_die(\"cyclic data global\");sl_g%zu_state=1u;sl_eval_enter();\n",
        id,id,id,id,id,id,id)) return false;
    if (!emit_expression(emitter, definition->value, NULL, 0u, "", &result)) return false;
    return text_printf(&emitter->output,
        "sl_g%zu_cache=t%zu;sl_g%zu_state=2u;sl_eval_leave();return t%zu;}\n",
        id,result,id,result);
}

static bool emit_main(Emitter *emitter, const char *symbol) {
    Function *function = find_function(emitter, symbol);
    size_t entry = definition_id(emitter, symbol);
    const char *keep =
        "(void)&sl_int_literal;(void)&sl_int_add;(void)&sl_int_sub;(void)&sl_int_mul;"
        "(void)&sl_int_compare;(void)&sl_int_to_bytes;(void)&sl_con;(void)&sl_bytes;"
        "(void)&sl_closure;(void)&sl_apply;(void)&sl_int_u64;(void)&sl_int_u64_value;"
        "(void)&sl_trap_bytes;(void)&sl_exit_code;(void)&sl_print_value;"
        "(void)&sl_int_size;(void)&sl_block_get;(void)&sl_block_new;(void)&sl_block_release;";
    if (function != NULL) {
        size_t unit = constructor_id(emitter, "sloph::Unit::Unit");
        size_t success = constructor_id(emitter, "os::process::Exit::Success");
        size_t failure = constructor_id(emitter, "os::process::Exit::Failure");
        return text_printf(&emitter->output,
            "int main(void){%sSlValue *argument=sl_con(%zuu,0u,NULL);SlValue *result=sl_f%zu(argument);if(result->kind!=1u)sl_die(\"main did not return Exit\");int status=2;if(result->as.con.tag==%zuu)status=0;else if(result->as.con.tag==%zuu&&result->as.con.count==1u)status=sl_exit_code(result->as.con.field[0]);else sl_die(\"main returned invalid Exit\");sl_flush_output();sl_destroy();return status;}\n",
            keep,unit,entry,success,failure);
    }
    return text_printf(&emitter->output,
        "int main(void){%sSlValue *result=sl_g%zu();sl_print_value(result);sl_char('\\n');sl_flush_output();sl_destroy();return 0;}\n",
        keep,entry);
}

static void emitter_destroy(Emitter *emitter) {
    size_t index;
    for (index = 0u; index < emitter->function_count; ++index)
        free(emitter->functions[index].parameters);
    for (index = 0u; index < emitter->lambda_count; ++index)
        free((void *)emitter->lambdas[index].captures);
    free(emitter->lambdas); free(emitter->constructors);
    free(emitter->functions); free(emitter->definitions);
}

static SlophStatus validate_runtime_contracts(Emitter *emitter,
                                              const char *symbol) {
    size_t index;
    Function *entry = find_function(emitter, symbol);
    if (entry != NULL) {
        const SlophCoreType *type = entry->definition->type;
        while (type->kind == SLOPH_TYPE_FORALL) type = type->as.forall.body;
        if (entry->parameter_count != 1u ||
            type->kind != SLOPH_TYPE_FUNCTION ||
            !type_is(type->as.function.parameter, SLOPH_TYPE_NAMED,
                     "sloph::Unit") ||
            !type_is(type->as.function.result, SLOPH_TYPE_NAMED,
                     "os::process::Exit") ||
            constructor_id(emitter, "sloph::Unit::Unit") == SIZE_MAX ||
            constructor_id(emitter, "os::process::Exit::Success") == SIZE_MAX ||
            constructor_id(emitter, "os::process::Exit::Failure") == SIZE_MAX)
            return backend_failure(emitter, "backend.c11.entry_shape",
                "a native function entry must have type Unit -> os::process::Exit",
                entry->definition->span);
    }
    for (index = 0u; index < emitter->unit->foreign_binding_count; ++index) {
        SlophCoreForeignBinding *binding = &emitter->unit->foreign_bindings[index];
        const char *error_type = NULL;
        char *native_error = NULL, *interrupted = NULL;
        bool valid = true;
        if (strcmp(binding->adapter, "unavailable") == 0) continue;
        if (strcmp(binding->adapter, "borrowed_bytes_write") == 0) {
            valid = result_shape(binding->result, &error_type) &&
                constructor_id(emitter, "sloph::Result::Ok") != SIZE_MAX &&
                constructor_id(emitter, "sloph::Result::Err") != SIZE_MAX;
            native_error = qualified_constructor(error_type, "Native");
            valid = valid && native_error != NULL &&
                constructor_id(emitter, native_error) != SIZE_MAX;
            if (strcmp(binding->adapter, "borrowed_bytes_write") == 0) {
                interrupted = qualified_constructor(error_type, "Interrupted");
                valid = valid && interrupted != NULL &&
                    constructor_id(emitter, interrupted) != SIZE_MAX;
            }
        }
        free(native_error); free(interrupted);
        if (!valid)
            return backend_failure(emitter, "backend.c11.foreign_adapter",
                "foreign adapter constructors are absent from the Core unit",
                emitter->unit->span);
    }
    return SLOPH_STATUS_OK;
}

static SlophStatus emitter_prepare(Emitter *emitter, const char *symbol) {
    size_t index, constructor_count = 0u;
    bool found = false;
    emitter->definitions = calloc(emitter->unit->definition_count != 0u ?
                                  emitter->unit->definition_count : 1u,
                                  sizeof(*emitter->definitions));
    emitter->functions = calloc(emitter->unit->definition_count != 0u ?
                                emitter->unit->definition_count : 1u,
                                sizeof(*emitter->functions));
    if (emitter->definitions == NULL || emitter->functions == NULL)
        return SLOPH_STATUS_OUT_OF_MEMORY;
    for (index = 0u; index < emitter->unit->definition_count; ++index) {
        SlophCoreDefinition *definition = &emitter->unit->definitions[index];
        emitter->definitions[index] = definition;
        if (strcmp(definition->name, symbol) == 0) found = true;
    }
    qsort(emitter->definitions, emitter->unit->definition_count,
          sizeof(*emitter->definitions), compare_definitions);
    if (!found) return backend_failure(emitter, "backend.c11.unknown_symbol",
                                       "unknown backend entry symbol", emitter->unit->span);
    for (index = 0u; index < emitter->unit->definition_count; ++index) {
        SlophCoreDefinition *definition = emitter->definitions[index];
        size_t arity = function_arity(definition->type), parameter = 0u;
        SlophCoreExpr *body = definition->value;
        Function *function;
        if (arity == 0u) continue;
        function = &emitter->functions[emitter->function_count++];
        function->definition = definition;
        function->parameters = calloc(arity, sizeof(*function->parameters));
        if (function->parameters == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
        while (body->kind == SLOPH_EXPR_LAM && body->as.lam.binder.is_type)
            body = body->as.lam.body;
        while (body->kind == SLOPH_EXPR_LAM && !body->as.lam.binder.is_type) {
            if (parameter < arity) function->parameters[parameter] = &body->as.lam.binder;
            ++parameter; body = body->as.lam.body;
        }
        if (parameter != arity)
            return backend_failure(emitter, "backend.c11.function_shape",
                "a backend function must be a direct top-level lambda chain",
                definition->value->span);
        function->parameter_count = arity; function->body = body;
    }
    if (emitter->unit->version == 0 && find_function(emitter, symbol) != NULL)
        return backend_failure(emitter, "backend.c11.function_entry",
            "the C11 backend entry symbol must be a data definition",
            find_function(emitter, symbol)->definition->span);
    for (index = 0u; index < emitter->unit->type_count; ++index)
        constructor_count += emitter->unit->types[index].constructor_count;
    emitter->constructors = calloc(constructor_count != 0u ? constructor_count : 1u,
                                   sizeof(*emitter->constructors));
    if (emitter->constructors == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    for (index = 0u; index < emitter->unit->type_count; ++index) {
        size_t item;
        for (item = 0u; item < emitter->unit->types[index].constructor_count; ++item)
            emitter->constructors[emitter->constructor_count++] =
                emitter->unit->types[index].constructors[item].name;
    }
    qsort(emitter->constructors, emitter->constructor_count,
          sizeof(*emitter->constructors), compare_names);
    for (index = 0u; index < emitter->unit->definition_count; ++index) {
        Function *function = find_function(emitter, emitter->definitions[index]->name);
        SlophCoreExpr *expression = function != NULL ? function->body : emitter->definitions[index]->value;
        if (!collect_lambdas(emitter, expression)) return SLOPH_STATUS_OUT_OF_MEMORY;
    }
    if (emitter->unit->version == 0) {
        for (index = 0u; index < emitter->unit->definition_count; ++index) {
            Function *function = find_function(emitter, emitter->definitions[index]->name);
            SlophStatus status;
            size_t parameter;
            if (function != NULL)
                for (parameter = 0u; parameter < function->parameter_count;
                     ++parameter)
                    if (function->parameters[parameter]->type->kind ==
                        SLOPH_TYPE_FUNCTION)
                        return higher_order_failure(emitter, "function parameter",
                            function->parameters[parameter]->span);
            status = check_v0_expression(
                emitter, function != NULL ? function->body : emitter->definitions[index]->value,
                false);
            if (status != SLOPH_STATUS_OK) return status;
        }
    }
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_heartwood_to_timber(SlophContext *context,
                                      SlophCoreUnit *unit,
                                      const char *symbol,
                                      char **out_text,
                                      size_t *out_length) {
    Emitter emitter;
    SlophStatus status;
    size_t index;
    const char **headers = NULL;
    size_t header_count = 0u;
    if (context == NULL || unit == NULL || symbol == NULL || out_text == NULL ||
        out_length == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
    *out_text = NULL; *out_length = 0u;
    status = sloph_core_validate(context, unit);
    if (status != SLOPH_STATUS_OK) return status;
    memset(&emitter, 0, sizeof(emitter));
    emitter.context = context; emitter.unit = unit;
    emitter.output.limit = sloph_context_limits(context)->project_bytes;
    status = validate_foreign_bindings(&emitter);
    if (status != SLOPH_STATUS_OK) goto done;
    status = emitter_prepare(&emitter, symbol);
    if (status != SLOPH_STATUS_OK) goto done;
    status = validate_runtime_contracts(&emitter, symbol);
    if (status != SLOPH_STATUS_OK) goto done;
    headers = calloc(unit->foreign_binding_count != 0u ?
                     unit->foreign_binding_count : 1u, sizeof(*headers));
    if (headers == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
    for (index = 0u; index < unit->foreign_binding_count; ++index)
        if (!name_present(headers, header_count, unit->foreign_bindings[index].header))
            headers[header_count++] = unit->foreign_bindings[index].header;
    qsort(headers, header_count, sizeof(*headers), compare_names);
    for (index = 0u; index < header_count; ++index)
        if (!text_printf(&emitter.output, "#include \"%s\"\n", headers[index]))
            goto output_failed;
    for (index = 0u; index < sizeof(sloph_timber_runtime) / sizeof(sloph_timber_runtime[0]); ++index)
        if (!text_puts(&emitter.output, sloph_timber_runtime[index])) goto output_failed;
    if (!emit_declarations(&emitter) || !emit_dispatch(&emitter) ||
        !emit_printer(&emitter)) goto output_failed;
    for (index = 0u; index < emitter.function_count; ++index)
        if (!emit_function(&emitter, &emitter.functions[index])) goto output_failed;
    for (index = 0u; index < emitter.lambda_count; ++index)
        if (!emit_lambda(&emitter, &emitter.lambdas[index])) goto output_failed;
    for (index = 0u; index < unit->definition_count; ++index)
        if (find_function(&emitter, emitter.definitions[index]->name) == NULL &&
            !emit_global(&emitter, emitter.definitions[index])) goto output_failed;
    if (!emit_main(&emitter, symbol)) goto output_failed;
    {
        const SlophAllocator *allocator = sloph_context_allocator(context);
        char *result = allocator->allocate(allocator->user_data,
                                           emitter.output.length + 1u);
        if (result == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
        memcpy(result, emitter.output.data, emitter.output.length + 1u);
        *out_text = result; *out_length = emitter.output.length;
    }
    status = SLOPH_STATUS_OK; goto done;
output_failed:
    if (emitter.output.out_of_memory) {
        status = SLOPH_STATUS_OUT_OF_MEMORY;
    } else {
        status = backend_failure(&emitter, "backend.c11.output_limit",
                                 "generated C11 exceeds the configured project byte limit",
                                 unit->span);
    }
done:
    free(headers); free(emitter.output.data); emitter_destroy(&emitter); return status;
}

SlophStatus sloph_backend_emit_c11(SlophContext *context,
                                   SlophCoreUnit *unit,
                                   const char *symbol,
                                   char **out_text,
                                   size_t *out_length) {
    return sloph_heartwood_to_timber(context, unit, symbol, out_text, out_length);
}

size_t sloph_heartwood_foreign_requirement_count(const SlophCoreUnit *unit) {
    return unit != NULL ? unit->foreign_binding_count : 0u;
}

SlophStatus sloph_heartwood_foreign_requirement(
    const SlophCoreUnit *unit, size_t index,
    SlophForeignRequirementView *out_requirement) {
    if (unit == NULL || out_requirement == NULL ||
        index >= unit->foreign_binding_count) return SLOPH_STATUS_INVALID_ARGUMENT;
    out_requirement->identity = unit->foreign_bindings[index].identity;
    out_requirement->provider = unit->foreign_bindings[index].provider;
    out_requirement->symbol = unit->foreign_bindings[index].symbol;
    out_requirement->header = unit->foreign_bindings[index].header;
    return SLOPH_STATUS_OK;
}
