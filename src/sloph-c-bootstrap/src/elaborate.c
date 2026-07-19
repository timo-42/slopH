#include "sloph/compiler.h"

#include "core_internal.h"
#include "project_internal.h"
#include "syntax_internal.h"
#include "yyjson.h"

#include <stdlib.h>
#include <stdio.h>
#include <string.h>

typedef struct LowerEnv {
    SlophContext *context;
    const SlophProject *project;
    const SlophProjectModule *module;
    const char **locals;
    const SlophSyntaxType **local_types;
    const SlophSyntaxExpr **local_values;
    size_t local_count;
    const char **type_variables;
    size_t type_variable_count;
} LowerEnv;

static char *copy_text(const char *text);
static SlophStatus fail(SlophContext *context, const char *code,
                        const char *phase, const char *message,
                        SlophSpan span);
static char *resolve_name(LowerEnv *env, const char *name, int wanted_kind,
                          SlophSpan span, const void **out_decl);

static const SlophProjectModule *find_module(const SlophProject *project,
                                              const char *name) {
    size_t i;
    for (i = 0u; i < project->module_count; ++i)
        if (strcmp(project->modules[i].name, name) == 0) return &project->modules[i];
    return NULL;
}

static int module_decl(const SlophProjectModule *module, const char *name,
                       int *public_, int *kind, const void **declaration) {
    size_t i;
    for (i = 0u; i < module->syntax->type_count; ++i)
        if (strcmp(module->syntax->types[i].name, name) == 0) {
            *public_ = module->syntax->types[i].public_; *kind = 0;
            *declaration = &module->syntax->types[i]; return 1;
        }
    for (i = 0u; i < module->syntax->function_count; ++i)
        if (strcmp(module->syntax->functions[i].name, name) == 0) {
            *public_ = module->syntax->functions[i].public_; *kind = 1;
            *declaration = &module->syntax->functions[i]; return 1;
        }
    for (i = 0u; i < module->syntax->value_count; ++i)
        if (strcmp(module->syntax->values[i].name, name) == 0) {
            *public_ = module->syntax->values[i].public_; *kind = 2;
            *declaration = &module->syntax->values[i]; return 1;
        }
    return 0;
}

static int module_export(const SlophProject *project,
                         const SlophProjectModule *module, const char *name,
                         int *public_, int *kind, const void **declaration,
                         const SlophProjectModule **owner) {
    size_t i, j;
    if (module_decl(module, name, public_, kind, declaration) && *public_) {
        *owner = module; return 1;
    }
    for (i = 0u; i < module->syntax->import_count; ++i) {
        const SlophSyntaxImport *import_ = &module->syntax->imports[i];
        const SlophSyntaxDirectImport *direct;
        const SlophProjectModule *target;
        if (import_->kind != SLOPH_SYNTAX_IMPORT_DIRECT) continue;
        direct = &import_->as.direct;
        if (!direct->public_) continue;
        for (j = 0u; j < direct->name_count; ++j)
            if (strcmp(direct->names[j], name) == 0) {
                target = find_module(project, direct->module);
                if (target != NULL && module_decl(target, name, public_, kind,
                                                  declaration) && *public_) {
                    *owner = target; return 1;
                }
            }
    }
    return 0;
}

static int is_local(const LowerEnv *env, const char *name) {
    size_t i;
    for (i = 0u; i < env->local_count; ++i)
        if (strcmp(env->locals[i], name) == 0) return 1;
    return 0;
}

static const SlophSyntaxType *local_type(const LowerEnv *env, const char *name) {
    size_t i;
    for (i = 0u; i < env->local_count; ++i)
        if (strcmp(env->locals[i], name) == 0)
            return env->local_types != NULL ? env->local_types[i] : NULL;
    return NULL;
}

static const SlophSyntaxExpr *local_value(const LowerEnv *env,
                                          const char *name) {
    size_t i;
    for (i = 0u; i < env->local_count; ++i)
        if (strcmp(env->locals[i], name) == 0)
            return env->local_values != NULL ? env->local_values[i] : NULL;
    return NULL;
}

static const SlophSyntaxType *infer_source_type(LowerEnv *env,
                                                const SlophSyntaxExpr *expression,
                                                const SlophProjectModule **out_module) {
    if (expression == NULL) return NULL;
    if (expression->kind == SLOPH_SYNTAX_EXPR_LOCAL)
        return local_type(env, expression->as.name);
    if (expression->kind == SLOPH_SYNTAX_EXPR_CALL) {
        const SlophSyntaxExpr *target = expression->as.call.function;
        const SlophSyntaxFunction *function = NULL;
        char *resolved;
        if (target->kind != SLOPH_SYNTAX_EXPR_LOCAL &&
            target->kind != SLOPH_SYNTAX_EXPR_GLOBAL) return NULL;
        resolved = resolve_name(env, target->as.name, 1, target->span,
                                (const void **)&function);
        if (resolved != NULL && out_module != NULL) {
            size_t i;
            for (i = 0u; i < env->project->module_count; ++i) {
                size_t prefix = strlen(env->project->modules[i].name);
                if (strncmp(resolved, env->project->modules[i].name, prefix) == 0 &&
                    resolved[prefix] == ':' && resolved[prefix + 1u] == ':') {
                    *out_module = &env->project->modules[i]; break;
                }
            }
        }
        free(resolved);
        return function != NULL ? function->result_type : NULL;
    }
    return NULL;
}

static int is_type_variable(const LowerEnv *env, const char *name) {
    size_t i;
    for (i = 0u; i < env->type_variable_count; ++i)
        if (strcmp(env->type_variables[i], name) == 0) return 1;
    return 0;
}

static char *qualified(const char *module, const char *name) {
    size_t a = strlen(module), b = strlen(name);
    char *result = malloc(a + b + 3u);
    if (result == NULL) return NULL;
    memcpy(result, module, a); memcpy(result + a, "::", 2u);
    memcpy(result + a + 2u, name, b + 1u);
    return result;
}

static char *resolve_name(LowerEnv *env, const char *name, int wanted_kind,
                          SlophSpan span, const void **out_decl) {
    size_t i, j;
    int public_, kind;
    const void *decl = NULL;
    if (strstr(name, "::") != NULL) {
        const char *last = strrchr(name, ':');
        size_t module_length = last != NULL && last > name ?
                               (size_t)(last - 1 - name) : 0u;
        char *module_name = malloc(module_length + 1u);
        const SlophProjectModule *owner;
        if (module_name == NULL) return NULL;
        memcpy(module_name, name, module_length); module_name[module_length] = '\0';
        owner = find_module(env->project, module_name);
        free(module_name);
        if (owner != NULL && module_decl(owner, last + 1, &public_, &kind, &decl) &&
            (wanted_kind < 0 || wanted_kind == kind)) {
            if (out_decl != NULL) *out_decl = decl;
            return copy_text(name);
        }
        /* Continue to the common unknown-name diagnostic below. */
        goto unknown;
    }
    if (module_decl(env->module, name, &public_, &kind, &decl)) {
        if (wanted_kind < 0 || kind == wanted_kind) {
            if (out_decl != NULL) *out_decl = decl;
            return qualified(env->module->name, name);
        }
    }
    for (i = 0u; i < env->module->syntax->import_count; ++i) {
        const SlophSyntaxImport *import_ = &env->module->syntax->imports[i];
        const SlophSyntaxDirectImport *direct;
        const SlophProjectModule *target;
        if (import_->kind != SLOPH_SYNTAX_IMPORT_DIRECT) continue;
        direct = &import_->as.direct;
        target = find_module(env->project, direct->module);
        if (target == NULL) continue;
        for (j = 0u; j < direct->name_count; ++j) {
            if (strcmp(direct->names[j], name) != 0) continue;
            { const SlophProjectModule *owner = target;
            if (module_export(env->project, target, name, &public_, &kind, &decl,
                              &owner) && public_ &&
                (wanted_kind < 0 || wanted_kind == kind)) {
                if (out_decl != NULL) *out_decl = decl;
                return qualified(owner->name, name);
            }
            }
        }
    }
    if (strcmp(env->module->name, "sloph") != 0 &&
        strncmp(env->module->name, "sloph::", 7u) != 0 &&
        strcmp(env->module->name, "core") != 0 &&
        strncmp(env->module->name, "core::", 6u) != 0 &&
        strcmp(env->module->name, "prelude") != 0 &&
        strncmp(env->module->name, "prelude::", 9u) != 0) {
        const SlophProjectModule *prelude = find_module(env->project, "prelude");
        const SlophProjectModule *owner = prelude;
        if (prelude != NULL && module_export(env->project, prelude, name, &public_,
                                             &kind, &decl, &owner) &&
            public_ && (wanted_kind < 0 || wanted_kind == kind)) {
            if (out_decl != NULL) *out_decl = decl;
            return qualified(owner->name, name);
        }
    }
unknown:
    {
        char details[1024], message[1024];
        (void)snprintf(details, sizeof(details),
                       "{\"module\":\"%s\",\"name\":\"%s\"}",
                       env->module->name, name);
        (void)snprintf(message, sizeof(message),
                       "unknown or inaccessible name '%s'", name);
        (void)sloph_context_add_diagnostic_full(env->context,
            "project.resolve.unknown_name", "resolve", message, details,
            span, SLOPH_SEVERITY_ERROR);
    }
    return NULL;
}

static char *resolve_constructor(LowerEnv *env, const char *source_name,
                                 SlophSpan span,
                                 const SlophSyntaxTypeDecl **out_owner,
                                 const SlophSyntaxConstructor **out_ctor) {
    const char *separator = strrchr(source_name, ':');
    size_t owner_length, i;
    char *owner, *resolved, *result;
    const SlophSyntaxTypeDecl *declaration = NULL;
    if (separator == NULL || separator == source_name || separator[-1] != ':')
        goto unknown;
    owner_length = (size_t)(separator - 1 - source_name);
    owner = malloc(owner_length + 1u);
    if (owner == NULL) return NULL;
    memcpy(owner, source_name, owner_length); owner[owner_length] = '\0';
    if (strcmp(owner, "Bool") == 0 || strcmp(owner, "sloph::Bool") == 0)
        resolved = copy_text("sloph::Bool");
    else if (strcmp(owner, "Unit") == 0 || strcmp(owner, "sloph::Unit") == 0)
        resolved = copy_text("sloph::Unit");
    else resolved = resolve_name(env, owner, 0, span, (const void **)&declaration);
    free(owner);
    if (resolved == NULL) return NULL;
    if (declaration != NULL && declaration->owned &&
        strncmp(resolved, env->module->name, strlen(env->module->name)) != 0) {
        free(resolved);
        { char details[1024], message[1024];
          (void)snprintf(details, sizeof(details),
                         "{\"constructor\":\"%s\"}", source_name);
          (void)snprintf(message, sizeof(message),
                         "unknown or inaccessible constructor '%s'", source_name);
          (void)sloph_context_add_diagnostic_full(env->context,
              "project.resolve.unknown_constructor", "resolve", message,
              details, span, SLOPH_SEVERITY_ERROR); }
        return NULL;
    }
    result = qualified(resolved, separator + 1);
    free(resolved);
    if (declaration != NULL) {
        for (i = 0u; i < declaration->constructor_count; ++i)
            if (strcmp(declaration->constructors[i].name, separator + 1) == 0) {
                if (out_owner != NULL) *out_owner = declaration;
                if (out_ctor != NULL) *out_ctor = &declaration->constructors[i];
                return result;
            }
        free(result);
        goto unknown;
    }
    if (out_owner != NULL) *out_owner = NULL;
    if (out_ctor != NULL) *out_ctor = NULL;
    return result;
unknown:
    { char details[1024], message[1024];
      (void)snprintf(details, sizeof(details),
                     "{\"constructor\":\"%s\"}", source_name);
      (void)snprintf(message, sizeof(message),
                     "unknown or inaccessible constructor '%s'", source_name);
      (void)sloph_context_add_diagnostic_full(env->context,
          "project.resolve.unknown_constructor", "resolve", message,
          details, span, SLOPH_SEVERITY_ERROR); }
    return NULL;
}

static char *copy_text(const char *text) {
    size_t size;
    char *copy;
    if (text == NULL) return NULL;
    size = strlen(text) + 1u;
    copy = malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

static SlophStatus fail(SlophContext *context, const char *code,
                        const char *phase, const char *message,
                        SlophSpan span) {
    SlophStatus status = sloph_context_add_diagnostic(context, code, phase,
                                                       message, span);
    return status == SLOPH_STATUS_OK ? SLOPH_STATUS_INVALID_ARGUMENT : status;
}

static SlophStatus oom(SlophContext *context) {
    SlophSpan span = {0u, 0u};
    return fail(context, "compiler.memory", "lower",
                "memory allocation failed while lowering Source", span);
}

static SlophCoreType *new_type(SlophCoreTypeKind kind) {
    SlophCoreType *type = calloc(1u, sizeof(*type));
    if (type != NULL) type->kind = kind;
    return type;
}

static SlophCoreType *clone_type(const SlophCoreType *source) {
    SlophCoreType *type;
    size_t i;
    if (source == NULL) return NULL;
    type = new_type(source->kind);
    if (type == NULL) return NULL;
    switch (source->kind) {
    case SLOPH_TYPE_NAMED:
    case SLOPH_TYPE_VARIABLE:
        type->as.name = copy_text(source->as.name);
        break;
    case SLOPH_TYPE_APPLIED:
        type->as.applied.constructor = copy_text(source->as.applied.constructor);
        type->as.applied.count = source->as.applied.count;
        if (type->as.applied.count != 0u)
            type->as.applied.items = calloc(type->as.applied.count,
                                            sizeof(*type->as.applied.items));
        for (i = 0u; i < type->as.applied.count &&
                     type->as.applied.items != NULL; ++i)
            type->as.applied.items[i] = clone_type(source->as.applied.items[i]);
        break;
    case SLOPH_TYPE_FUNCTION:
        type->as.function.mode = copy_text(source->as.function.mode);
        type->as.function.parameter = clone_type(source->as.function.parameter);
        type->as.function.result = clone_type(source->as.function.result);
        break;
    case SLOPH_TYPE_FORALL:
        type->as.forall.parameter = copy_text(source->as.forall.parameter);
        type->as.forall.body = clone_type(source->as.forall.body);
        break;
    case SLOPH_TYPE_INT:
    case SLOPH_TYPE_BYTES:
        break;
    }
    return type;
}

static SlophCoreExpr *new_expr(SlophCoreExprKind kind, SlophSpan span) {
    SlophCoreExpr *expression = calloc(1u, sizeof(*expression));
    if (expression != NULL) {
        expression->kind = kind;
        expression->span = span;
    }
    return expression;
}

static SlophCoreExpr *lower_block(LowerEnv *env,
                                  const SlophSyntaxBlock *block);
static SlophCoreType *lower_type(LowerEnv *env,
                                 const SlophSyntaxType *source);

static SlophCoreType *lower_substituted_type(
    LowerEnv *env, const SlophSyntaxType *source,
    const SlophSyntaxTypeDecl *owner, const SlophSyntaxType *scrutinee,
    const SlophProjectModule *type_module) {
    size_t i;
    if (source->kind == SLOPH_SYNTAX_TYPE_NAMED && owner != NULL &&
        scrutinee != NULL && scrutinee->kind == SLOPH_SYNTAX_TYPE_APPLIED) {
        for (i = 0u; i < owner->type_parameter_count &&
                     i < scrutinee->as.applied.count; ++i) {
            if (strcmp(source->as.name, owner->type_parameters[i]) == 0) {
                LowerEnv type_env = *env;
                type_env.module = type_module;
                return lower_type(&type_env, scrutinee->as.applied.items[i]);
            }
        }
    }
    if (source->kind == SLOPH_SYNTAX_TYPE_APPLIED) {
        SlophCoreType *type = new_type(SLOPH_TYPE_APPLIED);
        if (type == NULL) return NULL;
        type->as.applied.constructor = resolve_name(env,
            source->as.applied.constructor, 0, source->span, NULL);
        type->as.applied.count = source->as.applied.count;
        if (type->as.applied.count != 0u)
            type->as.applied.items = calloc(type->as.applied.count,
                                            sizeof(*type->as.applied.items));
        for (i = 0u; i < type->as.applied.count &&
                     type->as.applied.items != NULL; ++i)
            type->as.applied.items[i] = lower_substituted_type(
                env, source->as.applied.items[i], owner, scrutinee, type_module);
        return type;
    }
    if (source->kind == SLOPH_SYNTAX_TYPE_FUNCTION) {
        SlophCoreType *type = new_type(SLOPH_TYPE_FUNCTION);
        if (type == NULL) return NULL;
        type->as.function.mode = copy_text(source->as.function.mode != NULL ?
                                           source->as.function.mode : "own");
        type->as.function.parameter = lower_substituted_type(
            env, source->as.function.parameter, owner, scrutinee, type_module);
        type->as.function.result = lower_substituted_type(
            env, source->as.function.result, owner, scrutinee, type_module);
        return type;
    }
    return lower_type(env, source);
}

static SlophCoreType *lower_field_type(
    LowerEnv *env, const SlophSyntaxType *source,
    const SlophSyntaxTypeDecl *owner, const SlophCoreType *scrutinee) {
    SlophCoreType *type;
    size_t i;
    if (source->kind == SLOPH_SYNTAX_TYPE_NAMED && owner != NULL &&
        scrutinee != NULL && scrutinee->kind == SLOPH_TYPE_APPLIED) {
        for (i = 0u; i < owner->type_parameter_count &&
                     i < scrutinee->as.applied.count; ++i)
            if (strcmp(source->as.name, owner->type_parameters[i]) == 0)
                return clone_type(scrutinee->as.applied.items[i]);
    }
    if (source->kind == SLOPH_SYNTAX_TYPE_APPLIED) {
        type = new_type(SLOPH_TYPE_APPLIED);
        if (type == NULL) return NULL;
        type->as.applied.constructor = resolve_name(
            env, source->as.applied.constructor, 0, source->span, NULL);
        type->as.applied.count = source->as.applied.count;
        if (type->as.applied.count != 0u)
            type->as.applied.items = calloc(type->as.applied.count,
                                            sizeof(*type->as.applied.items));
        for (i = 0u; i < type->as.applied.count &&
                     type->as.applied.items != NULL; ++i)
            type->as.applied.items[i] = lower_field_type(
                env, source->as.applied.items[i], owner, scrutinee);
        return type;
    }
    if (source->kind == SLOPH_SYNTAX_TYPE_FUNCTION) {
        type = new_type(SLOPH_TYPE_FUNCTION);
        if (type == NULL) return NULL;
        type->as.function.mode = copy_text(source->as.function.mode != NULL ?
                                           source->as.function.mode : "own");
        type->as.function.parameter = lower_field_type(
            env, source->as.function.parameter, owner, scrutinee);
        type->as.function.result = lower_field_type(
            env, source->as.function.result, owner, scrutinee);
        return type;
    }
    return lower_type(env, source);
}

static int core_type_owned(const SlophProject *project,
                           const SlophCoreType *type) {
    const char *name = type->kind == SLOPH_TYPE_NAMED ? type->as.name :
                       type->kind == SLOPH_TYPE_APPLIED ?
                       type->as.applied.constructor : NULL;
    size_t i, j;
    if (name == NULL) return 0;
    for (i = 0u; i < project->module_count; ++i)
        for (j = 0u; j < project->modules[i].syntax->type_count; ++j) {
            const SlophSyntaxTypeDecl *decl = &project->modules[i].syntax->types[j];
            char *identity = qualified(project->modules[i].name, decl->name);
            int match = identity != NULL && strcmp(identity, name) == 0 && decl->owned;
            free(identity);
            if (match) return 1;
        }
    return 0;
}

static void collect_moves_expr(LowerEnv *env, const SlophSyntaxExpr *expression,
                               const char *name, SlophSpan *spans,
                               size_t *count, size_t capacity);

static void collect_moves_block(LowerEnv *env, const SlophSyntaxBlock *block,
                                const char *name, SlophSpan *spans,
                                size_t *count, size_t capacity) {
    size_t i;
    for (i = 0u; i < block->statement_count; ++i) {
        const SlophSyntaxStatement *statement = &block->statements[i];
        collect_moves_expr(env, statement->kind == SLOPH_SYNTAX_STMT_LET ?
                           statement->as.let.value : statement->as.defer_call,
                           name, spans, count, capacity);
    }
    collect_moves_expr(env, block->result, name, spans, count, capacity);
}

static void record_move(SlophSpan span, SlophSpan *spans, size_t *count,
                        size_t capacity) {
    if (*count < capacity) spans[*count] = span;
    ++*count;
}

static void collect_moves_expr(LowerEnv *env, const SlophSyntaxExpr *expression,
                               const char *name, SlophSpan *spans,
                               size_t *count, size_t capacity) {
    size_t i;
    if (expression == NULL) return;
    switch (expression->kind) {
    case SLOPH_SYNTAX_EXPR_CALL: {
        const SlophSyntaxFunction *function = NULL;
        const SlophSyntaxExpr *target = expression->as.call.function;
        char *resolved = NULL;
        if (target->kind == SLOPH_SYNTAX_EXPR_LOCAL ||
            target->kind == SLOPH_SYNTAX_EXPR_GLOBAL)
            resolved = resolve_name(env, target->as.name, 1, target->span,
                                    (const void **)&function);
        free(resolved);
        for (i = 0u; i < expression->as.call.argument_count; ++i) {
            const SlophSyntaxExpr *argument = expression->as.call.arguments[i];
            if (argument->kind == SLOPH_SYNTAX_EXPR_LOCAL &&
                strcmp(argument->as.name, name) == 0 &&
                (function == NULL || i >= function->parameter_count ||
                 function->parameters[i].mode == NULL ||
                 strcmp(function->parameters[i].mode, "borrow") != 0))
                record_move(argument->span, spans, count, capacity);
            else collect_moves_expr(env, argument, name, spans, count, capacity);
        }
        break;
    }
    case SLOPH_SYNTAX_EXPR_CONSTRUCTOR:
        for (i = 0u; i < expression->as.constructor.argument_count; ++i) {
            const SlophSyntaxExpr *argument = expression->as.constructor.arguments[i];
            if (argument->kind == SLOPH_SYNTAX_EXPR_LOCAL &&
                strcmp(argument->as.name, name) == 0)
                record_move(argument->span, spans, count, capacity);
            else collect_moves_expr(env, argument, name, spans, count, capacity);
        }
        break;
    case SLOPH_SYNTAX_EXPR_BINARY:
        collect_moves_expr(env, expression->as.binary.left, name, spans, count, capacity);
        collect_moves_expr(env, expression->as.binary.right, name, spans, count, capacity);
        break;
    case SLOPH_SYNTAX_EXPR_LAMBDA:
        collect_moves_block(env, expression->as.lambda.body, name, spans, count, capacity);
        break;
    case SLOPH_SYNTAX_EXPR_IF:
        collect_moves_expr(env, expression->as.if_.condition, name, spans, count, capacity);
        collect_moves_block(env, expression->as.if_.then_body, name, spans, count, capacity);
        collect_moves_block(env, expression->as.if_.else_body, name, spans, count, capacity);
        break;
    case SLOPH_SYNTAX_EXPR_PRIMITIVE:
        for (i = 0u; i < expression->as.primitive.argument_count; ++i)
            collect_moves_expr(env, expression->as.primitive.arguments[i], name,
                               spans, count, capacity);
        break;
    case SLOPH_SYNTAX_EXPR_CASE:
        collect_moves_expr(env, expression->as.case_.scrutinee, name, spans, count, capacity);
        for (i = 0u; i < expression->as.case_.alternative_count; ++i)
            collect_moves_block(env, expression->as.case_.alternatives[i].body,
                                name, spans, count, capacity);
        break;
    default: break;
    }
}

static SlophCoreType *infer_expr_type(LowerEnv *env,
                                      const SlophSyntaxExpr *expression) {
    const SlophSyntaxType *source_type;
    const SlophProjectModule *type_module = env->module;
    if (expression->kind == SLOPH_SYNTAX_EXPR_INT) return new_type(SLOPH_TYPE_INT);
    if (expression->kind == SLOPH_SYNTAX_EXPR_BYTES) return new_type(SLOPH_TYPE_BYTES);
    if (expression->kind == SLOPH_SYNTAX_EXPR_LOCAL) {
        const SlophSyntaxType *type = local_type(env, expression->as.name);
        const SlophSyntaxExpr *value = local_value(env, expression->as.name);
        if (type != NULL && type->kind == SLOPH_SYNTAX_TYPE_INFERRED &&
            value != NULL)
            return infer_expr_type(env, value);
        return type != NULL && type->kind != SLOPH_SYNTAX_TYPE_INFERRED ?
               lower_type(env, type) : NULL;
    }
    if (expression->kind == SLOPH_SYNTAX_EXPR_BINARY) {
        if (strcmp(expression->as.binary.operator_, "==") == 0 ||
            strcmp(expression->as.binary.operator_, "<") == 0) {
            SlophCoreType *type = new_type(SLOPH_TYPE_NAMED);
            if (type != NULL) type->as.name = copy_text("sloph::Bool");
            return type;
        }
        return new_type(SLOPH_TYPE_INT);
    }
    if (expression->kind == SLOPH_SYNTAX_EXPR_IF)
        return infer_expr_type(env, expression->as.if_.then_body->result);
    if (expression->kind == SLOPH_SYNTAX_EXPR_CASE)
        return lower_type(env, expression->as.case_.result_type);
    if (expression->kind == SLOPH_SYNTAX_EXPR_PRIMITIVE) {
        if (strcmp(expression->as.primitive.name, "int.to_bytes") == 0)
            return new_type(SLOPH_TYPE_BYTES);
        if (strcmp(expression->as.primitive.name, "int.equal") == 0 ||
            strcmp(expression->as.primitive.name, "int.less") == 0) {
            SlophCoreType *type = new_type(SLOPH_TYPE_NAMED);
            if (type != NULL) type->as.name = copy_text("sloph::Bool");
            return type;
        }
        return new_type(SLOPH_TYPE_INT);
    }
    if (expression->kind == SLOPH_SYNTAX_EXPR_CALL) {
        const SlophSyntaxExpr *target = expression->as.call.function;
        if (target->kind == SLOPH_SYNTAX_EXPR_LOCAL && is_local(env, target->as.name)) {
            const SlophSyntaxType *local = local_type(env, target->as.name);
            SlophCoreType *type = local != NULL ? lower_type(env, local) : NULL;
            size_t n = expression->as.call.argument_count;
            while (type != NULL && n-- != 0u && type->kind == SLOPH_TYPE_FUNCTION) {
                SlophCoreType *result = type->as.function.result;
                type->as.function.result = NULL;
                sloph_core_type_destroy(type);
                type = result;
            }
            return type;
        }
        if (target->kind == SLOPH_SYNTAX_EXPR_LOCAL ||
            target->kind == SLOPH_SYNTAX_EXPR_GLOBAL) {
            const SlophSyntaxFunction *function = NULL;
            char *resolved = resolve_name(env, target->as.name, 1, target->span,
                                          (const void **)&function);
            const SlophProjectModule *function_module = env->module;
            LowerEnv result_env;
            SlophCoreType *type;
            size_t i;
            SlophSyntaxTypeDecl substitutions;
            SlophSyntaxType applied_arguments;
            if (function == NULL) { free(resolved); return NULL; }
            if (resolved != NULL) {
                for (i = 0u; i < env->project->module_count; ++i) {
                    size_t prefix = strlen(env->project->modules[i].name);
                    if (strncmp(resolved, env->project->modules[i].name,
                                prefix) == 0 && resolved[prefix] == ':' &&
                        resolved[prefix + 1u] == ':') {
                        function_module = &env->project->modules[i];
                        break;
                    }
                }
            }
            free(resolved);
            memset(&substitutions, 0, sizeof(substitutions));
            memset(&applied_arguments, 0, sizeof(applied_arguments));
            substitutions.type_parameters = function->type_parameters;
            substitutions.type_parameter_count = function->type_parameter_count;
            applied_arguments.kind = SLOPH_SYNTAX_TYPE_APPLIED;
            applied_arguments.as.applied.items = expression->as.call.type_arguments;
            applied_arguments.as.applied.count = expression->as.call.type_argument_count;
            result_env = *env;
            result_env.module = function_module;
            type = lower_substituted_type(&result_env, function->result_type,
                                          &substitutions, &applied_arguments,
                                          env->module);
            for (i = function->parameter_count;
                 i > expression->as.call.argument_count; --i) {
                SlophCoreType *outer = new_type(SLOPH_TYPE_FUNCTION);
                if (outer == NULL) { sloph_core_type_destroy(type); return NULL; }
                outer->as.function.mode = copy_text(
                    function->parameters[i - 1u].mode != NULL ?
                    function->parameters[i - 1u].mode : "own");
                outer->as.function.parameter = lower_substituted_type(
                    &result_env, function->parameters[i - 1u].type, &substitutions,
                    &applied_arguments, env->module);
                outer->as.function.result = type;
                type = outer;
            }
            return type;
        }
    }
    if (expression->kind == SLOPH_SYNTAX_EXPR_CONSTRUCTOR) {
        const char *separator = strrchr(expression->as.constructor.constructor, ':');
        SlophCoreType *type;
        char *constructor = resolve_constructor(env,
                               expression->as.constructor.constructor,
                               expression->span, NULL, NULL);
        if (constructor == NULL || separator == NULL) { free(constructor); return NULL; }
        type = expression->as.constructor.type_argument_count != 0u ?
               new_type(SLOPH_TYPE_APPLIED) : new_type(SLOPH_TYPE_NAMED);
        if (type != NULL && type->kind == SLOPH_TYPE_NAMED) {
            char *last = strrchr(constructor, ':');
            size_t length = (size_t)(last - 1 - constructor);
            type->as.name = malloc(length + 1u);
            if (type->as.name != NULL) {
                memcpy(type->as.name, constructor, length); type->as.name[length] = '\0';
            }
        } else if (type != NULL) {
            size_t i;
            char *last = strrchr(constructor, ':');
            size_t length = (size_t)(last - 1 - constructor);
            type->as.applied.constructor = malloc(length + 1u);
            if (type->as.applied.constructor != NULL) {
                memcpy(type->as.applied.constructor, constructor, length);
                type->as.applied.constructor[length] = '\0';
            }
            type->as.applied.count = expression->as.constructor.type_argument_count;
            type->as.applied.items = calloc(type->as.applied.count,
                                            sizeof(*type->as.applied.items));
            for (i = 0u; i < type->as.applied.count && type->as.applied.items != NULL; ++i)
                type->as.applied.items[i] = lower_type(env,
                                    expression->as.constructor.type_arguments[i]);
        }
        free(constructor); return type;
    }
    source_type = infer_source_type(env, expression, &type_module);
    if (source_type != NULL) {
        LowerEnv type_env = *env;
        type_env.module = type_module;
        return lower_type(&type_env, source_type);
    }
    return NULL;
}

static SlophCoreType *lower_type(LowerEnv *env,
                                 const SlophSyntaxType *source) {
    SlophCoreType *type;
    size_t index;
    if (source == NULL) return NULL;
    if (source->kind == SLOPH_SYNTAX_TYPE_INT) return new_type(SLOPH_TYPE_INT);
    if (source->kind == SLOPH_SYNTAX_TYPE_NAMED) {
        if (is_type_variable(env, source->as.name)) {
            type = new_type(SLOPH_TYPE_VARIABLE);
            if (type != NULL) type->as.name = copy_text(source->as.name);
            if (type == NULL || type->as.name == NULL) {
                sloph_core_type_destroy(type); return NULL;
            }
            return type;
        }
        if (strcmp(source->as.name, "Int") == 0 ||
            strcmp(source->as.name, "core::Int") == 0)
            return new_type(SLOPH_TYPE_INT);
        if (strcmp(source->as.name, "Bytes") == 0 ||
            strcmp(source->as.name, "core::Bytes") == 0)
            return new_type(SLOPH_TYPE_BYTES);
        type = new_type(SLOPH_TYPE_NAMED);
        if (type == NULL) return NULL;
        if (strcmp(source->as.name, "Bool") == 0)
            type->as.name = copy_text("sloph::Bool");
        else if (strcmp(source->as.name, "Unit") == 0)
            type->as.name = copy_text("sloph::Unit");
        else
            type->as.name = resolve_name(env, source->as.name, 0, source->span, NULL);
        if (type->as.name == NULL) { sloph_core_type_destroy(type); return NULL; }
        return type;
    }
    if (source->kind == SLOPH_SYNTAX_TYPE_FUNCTION) {
        type = new_type(SLOPH_TYPE_FUNCTION);
        if (type == NULL) return NULL;
        type->as.function.mode = copy_text(source->as.function.mode != NULL ?
                                           source->as.function.mode : "own");
        type->as.function.parameter = lower_type(env, source->as.function.parameter);
        type->as.function.result = lower_type(env, source->as.function.result);
        if (type->as.function.mode == NULL || type->as.function.parameter == NULL ||
            type->as.function.result == NULL) {
            sloph_core_type_destroy(type); return NULL;
        }
        return type;
    }
    if (source->kind == SLOPH_SYNTAX_TYPE_APPLIED) {
        type = new_type(SLOPH_TYPE_APPLIED);
        if (type == NULL) return NULL;
        type->as.applied.constructor = resolve_name(env, source->as.applied.constructor,
                                                    0, source->span, NULL);
        type->as.applied.count = source->as.applied.count;
        if (type->as.applied.count != 0u)
            type->as.applied.items = calloc(type->as.applied.count,
                                            sizeof(*type->as.applied.items));
        if (type->as.applied.constructor == NULL ||
            (type->as.applied.count != 0u && type->as.applied.items == NULL)) {
            sloph_core_type_destroy(type); return NULL;
        }
        for (index = 0u; index < type->as.applied.count; ++index) {
            type->as.applied.items[index] = lower_type(env, source->as.applied.items[index]);
            if (type->as.applied.items[index] == NULL) {
                sloph_core_type_destroy(type); return NULL;
            }
        }
        return type;
    }
    (void)fail(env->context, "project.lower.type", "lower",
               "unsupported source type", source->span);
    return NULL;
}

static SlophCoreExpr *lower_expr(LowerEnv *env,
                                 const SlophSyntaxExpr *source) {
    SlophCoreExpr *result;
    size_t index;
    if (source == NULL) return NULL;
    if (source->kind == SLOPH_SYNTAX_EXPR_INT) {
        result = new_expr(SLOPH_EXPR_INT, source->span);
        if (result != NULL) result->as.integer = copy_text(source->as.integer);
        if (result == NULL || result->as.integer == NULL) {
            sloph_core_expr_destroy(result); return NULL;
        }
        return result;
    }
    if (source->kind == SLOPH_SYNTAX_EXPR_BYTES) {
        result = new_expr(SLOPH_EXPR_BYTES, source->span);
        if (result == NULL) return NULL;
        result->as.bytes.length = source->as.bytes.length;
        if (result->as.bytes.length != 0u) {
            result->as.bytes.data = malloc(result->as.bytes.length);
            if (result->as.bytes.data == NULL) { free(result); return NULL; }
            memcpy(result->as.bytes.data, source->as.bytes.data,
                   result->as.bytes.length);
        }
        return result;
    }
    if (source->kind == SLOPH_SYNTAX_EXPR_LOCAL ||
        source->kind == SLOPH_SYNTAX_EXPR_GLOBAL) {
        int local = source->kind == SLOPH_SYNTAX_EXPR_LOCAL && is_local(env, source->as.name);
        result = new_expr(local ? SLOPH_EXPR_LOCAL : SLOPH_EXPR_GLOBAL, source->span);
        if (result != NULL)
            result->as.name = local ? copy_text(source->as.name) :
                              resolve_name(env, source->as.name, -1, source->span, NULL);
        if (result == NULL || result->as.name == NULL) {
            sloph_core_expr_destroy(result); return NULL;
        }
        return result;
    }
    if (source->kind == SLOPH_SYNTAX_EXPR_PRIMITIVE) {
        result = new_expr(SLOPH_EXPR_PRIM, source->span);
        if (result == NULL) return NULL;
        result->as.prim.name = copy_text(source->as.primitive.name);
        result->as.prim.count = source->as.primitive.argument_count;
        if (result->as.prim.count != 0u)
            result->as.prim.items = calloc(result->as.prim.count,
                                           sizeof(*result->as.prim.items));
        if (result->as.prim.name == NULL ||
            (result->as.prim.count != 0u && result->as.prim.items == NULL)) {
            sloph_core_expr_destroy(result); return NULL;
        }
        for (index = 0u; index < result->as.prim.count; ++index) {
            result->as.prim.items[index] = lower_expr(env,
                                              source->as.primitive.arguments[index]);
            if (result->as.prim.items[index] == NULL) {
                sloph_core_expr_destroy(result); return NULL;
            }
        }
        return result;
    }
    if (source->kind == SLOPH_SYNTAX_EXPR_CONSTRUCTOR) {
        result = new_expr(SLOPH_EXPR_CON, source->span);
        if (result == NULL) return NULL;
        result->as.con.constructor = resolve_constructor(env,
                                      source->as.constructor.constructor,
                                      source->span, NULL, NULL);
        result->as.con.type_argument_count = source->as.constructor.type_argument_count;
        if (result->as.con.type_argument_count != 0u)
            result->as.con.type_arguments = calloc(result->as.con.type_argument_count,
                                                   sizeof(*result->as.con.type_arguments));
        result->as.con.field_count = source->as.constructor.argument_count;
        if (result->as.con.field_count != 0u)
            result->as.con.fields = calloc(result->as.con.field_count,
                                           sizeof(*result->as.con.fields));
        if (result->as.con.constructor == NULL ||
            (result->as.con.field_count != 0u && result->as.con.fields == NULL)) {
            sloph_core_expr_destroy(result); return NULL;
        }
        for (index = 0u; index < result->as.con.field_count; ++index) {
            result->as.con.fields[index] = lower_expr(env,
                                         source->as.constructor.arguments[index]);
            if (result->as.con.fields[index] == NULL) {
                sloph_core_expr_destroy(result); return NULL;
            }
        }
        for (index = 0u; index < result->as.con.type_argument_count; ++index) {
            result->as.con.type_arguments[index] = lower_type(env,
                                      source->as.constructor.type_arguments[index]);
            if (result->as.con.type_arguments[index] == NULL) {
                sloph_core_expr_destroy(result); return NULL;
            }
        }
        return result;
    }
    if (source->kind == SLOPH_SYNTAX_EXPR_LAMBDA) {
        LowerEnv active = *env;
        const char **locals = calloc(env->local_count + source->as.lambda.parameter_count,
                                     sizeof(*locals));
        const SlophSyntaxType **local_types = calloc(env->local_count +
                                      source->as.lambda.parameter_count,
                                      sizeof(*local_types));
        const SlophSyntaxExpr **local_values = calloc(env->local_count +
                                      source->as.lambda.parameter_count,
                                      sizeof(*local_values));
        if (locals == NULL || local_types == NULL || local_values == NULL) {
            free(locals); free(local_types); free(local_values); return NULL;
        }
        memcpy(locals, env->locals, env->local_count * sizeof(*locals));
        if (env->local_types != NULL)
            memcpy(local_types, env->local_types, env->local_count * sizeof(*local_types));
        if (env->local_values != NULL)
            memcpy(local_values, env->local_values,
                   env->local_count * sizeof(*local_values));
        for (index = 0u; index < source->as.lambda.parameter_count; ++index) {
            locals[active.local_count++] = source->as.lambda.parameters[index].name;
            local_types[env->local_count + index] = source->as.lambda.parameters[index].type;
        }
        active.locals = locals; active.local_types = local_types;
        active.local_values = local_values;
        result = lower_block(&active, source->as.lambda.body);
        for (index = source->as.lambda.parameter_count; result != NULL && index != 0u; --index) {
            const SlophSyntaxBinder *binder = &source->as.lambda.parameters[index - 1u];
            SlophCoreExpr *lambda = new_expr(SLOPH_EXPR_LAM, source->span);
            if (lambda == NULL) { sloph_core_expr_destroy(result); result = NULL; break; }
            lambda->as.lam.binder.name = copy_text(binder->name);
            lambda->as.lam.binder.mode = copy_text(binder->mode != NULL ? binder->mode : "own");
            lambda->as.lam.binder.type = lower_type(&active, binder->type);
            lambda->as.lam.binder.span = binder->span;
            lambda->as.lam.body = result; result = lambda;
        }
        free(locals); free(local_types); free(local_values);
        return result;
    }
    if (source->kind == SLOPH_SYNTAX_EXPR_IF) {
        SlophCoreExpr *condition = lower_expr(env, source->as.if_.condition);
        SlophCoreExpr *then_ = lower_block(env, source->as.if_.then_body);
        SlophCoreExpr *else_ = lower_block(env, source->as.if_.else_body);
        result = new_expr(SLOPH_EXPR_CASE, source->span);
        if (result == NULL || condition == NULL || then_ == NULL || else_ == NULL) {
            sloph_core_expr_destroy(condition); sloph_core_expr_destroy(then_);
            sloph_core_expr_destroy(else_); sloph_core_expr_destroy(result); return NULL;
        }
        result->as.case_.scrutinee = condition;
        result->as.case_.alternative_count = 2u;
        result->as.case_.alternatives = calloc(2u, sizeof(*result->as.case_.alternatives));
        result->as.case_.alternatives[0].constructor = copy_text("sloph::Bool::False");
        result->as.case_.alternatives[0].body = else_;
        result->as.case_.alternatives[0].span = source->span;
        result->as.case_.alternatives[1].constructor = copy_text("sloph::Bool::True");
        result->as.case_.alternatives[1].body = then_;
        result->as.case_.alternatives[1].span = source->span;
        result->as.case_.result_type = infer_expr_type(env,
                                      source->as.if_.then_body->result);
        if (result->as.case_.result_type == NULL) {
            sloph_core_expr_destroy(result); return NULL;
        }
        return result;
    }
    if (source->kind == SLOPH_SYNTAX_EXPR_CASE) {
        result = new_expr(SLOPH_EXPR_CASE, source->span);
        if (result == NULL) return NULL;
        result->as.case_.scrutinee = lower_expr(env, source->as.case_.scrutinee);
        result->as.case_.result_type = lower_type(env, source->as.case_.result_type);
        result->as.case_.alternative_count = source->as.case_.alternative_count;
        if (result->as.case_.alternative_count != 0u)
            result->as.case_.alternatives = calloc(result->as.case_.alternative_count,
                                                   sizeof(*result->as.case_.alternatives));
        for (index = 0u; index < result->as.case_.alternative_count; ++index) {
            const SlophSyntaxCaseAlternative *alternative = &source->as.case_.alternatives[index];
            const SlophSyntaxTypeDecl *owner = NULL;
            const SlophSyntaxConstructor *constructor = NULL;
            SlophCoreAlternative *target = &result->as.case_.alternatives[index];
            LowerEnv active = *env;
            const char **locals;
            const SlophSyntaxType **local_types;
            const SlophSyntaxExpr **local_values;
            size_t j;
            target->constructor = resolve_constructor(env, alternative->constructor,
                                                       alternative->span,
                                                       &owner, &constructor);
            target->binder_count = alternative->binder_count;
            target->span = alternative->span;
            if (target->binder_count != 0u)
                target->binders = calloc(target->binder_count, sizeof(*target->binders));
            locals = calloc(env->local_count + target->binder_count, sizeof(*locals));
            local_types = calloc(env->local_count + target->binder_count,
                                 sizeof(*local_types));
            local_values = calloc(env->local_count + target->binder_count,
                                  sizeof(*local_values));
            if (locals == NULL || local_types == NULL || local_values == NULL) {
                free(locals); free(local_types); free(local_values);
                sloph_core_expr_destroy(result); return NULL;
            }
            memcpy(locals, env->locals, env->local_count * sizeof(*locals));
            if (env->local_types != NULL)
                memcpy(local_types, env->local_types,
                       env->local_count * sizeof(*local_types));
            if (env->local_values != NULL)
                memcpy(local_values, env->local_values,
                       env->local_count * sizeof(*local_values));
            for (j = 0u; j < target->binder_count; ++j) {
                const SlophSyntaxBinder *binder = &alternative->binders[j];
                target->binders[j].name = copy_text(binder->name);
                target->binders[j].mode = copy_text(binder->mode != NULL ? binder->mode : "own");
                target->binders[j].span = binder->span;
                if (binder->type->kind == SLOPH_SYNTAX_TYPE_INFERRED &&
                    constructor != NULL && j < constructor->field_count) {
                    SlophCoreType *scrutinee_type = infer_expr_type(
                        env, source->as.case_.scrutinee);
                    const SlophSyntaxType *field_type = constructor->fields[j].type;
                    target->binders[j].type = lower_field_type(
                        env, field_type, owner, scrutinee_type);
                    sloph_core_type_destroy(scrutinee_type);
                } else target->binders[j].type = lower_type(env, binder->type);
                if (target->binders[j].type == NULL) {
                    sloph_core_expr_destroy(result);
                    free(locals); free(local_types); free(local_values); return NULL;
                }
                locals[active.local_count++] = binder->name;
                local_types[env->local_count + j] = binder->type;
            }
            if (!env->module->bundled) {
                for (j = 0u; j < target->binder_count; ++j) {
                    if (core_type_owned(env->project, target->binders[j].type)) {
                        SlophSpan moves[2];
                        size_t move_count = 0u;
                        char details[512], message[512];
                        collect_moves_block(&active, alternative->body,
                                            target->binders[j].name, moves,
                                            &move_count, 2u);
                        (void)snprintf(details, sizeof(details),
                                       "{\"local\":\"%s\"}",
                                       target->binders[j].name);
                        if (move_count == 0u) {
                            SlophSpan ownership_span = {
                                alternative->span.start,
                                alternative->body->span.end
                            };
                            (void)snprintf(message, sizeof(message),
                                "owned pattern field '%s' must be moved or explicitly dropped",
                                target->binders[j].name);
                            (void)sloph_context_add_diagnostic_full(env->context,
                                "core.validate.owned_not_consumed", "validate",
                                message, details, ownership_span,
                                SLOPH_SEVERITY_ERROR);
                            sloph_core_expr_destroy(result);
                            free(locals); free(local_types); free(local_values);
                            return NULL;
                        }
                        if (move_count > 1u) {
                            (void)snprintf(message, sizeof(message),
                                "owned local '%s' is used after it was moved",
                                target->binders[j].name);
                            (void)sloph_context_add_diagnostic_full(env->context,
                                "core.validate.use_after_move", "validate",
                                message, details, moves[0], SLOPH_SEVERITY_ERROR);
                            sloph_core_expr_destroy(result);
                            free(locals); free(local_types); free(local_values);
                            return NULL;
                        }
                    }
                }
            }
            (void)owner;
            active.locals = locals; active.local_types = local_types;
            active.local_values = local_values;
            target->body = lower_block(&active, alternative->body);
            free(locals); free(local_types); free(local_values);
        }
        if (result->as.case_.scrutinee == NULL || result->as.case_.result_type == NULL ||
            (result->as.case_.alternative_count != 0u && result->as.case_.alternatives == NULL)) {
            sloph_core_expr_destroy(result); return NULL;
        }
        return result;
    }
    if (source->kind == SLOPH_SYNTAX_EXPR_CALL) {
        const SlophSyntaxExpr *target = source->as.call.function;
        const SlophSyntaxFunction *function = NULL;
        char *name;
        if (env->module->syntax->version >= 1u) {
            if ((target->kind == SLOPH_SYNTAX_EXPR_LOCAL ||
                 target->kind == SLOPH_SYNTAX_EXPR_GLOBAL) &&
                !(target->kind == SLOPH_SYNTAX_EXPR_LOCAL &&
                  is_local(env, target->as.name))) {
                name = resolve_name(env, target->as.name, 1, target->span,
                                    (const void **)&function);
                if (name == NULL) return NULL;
                result = new_expr(SLOPH_EXPR_GLOBAL, target->span);
                if (result != NULL) result->as.name = name;
            } else {
                result = lower_expr(env, target);
            }
            if (result == NULL) return NULL;
            if (function != NULL && source->as.call.type_argument_count !=
                                    function->type_parameter_count) {
                sloph_core_expr_destroy(result);
                (void)fail(env->context, "project.resolve.type_argument_arity",
                           "resolve", "function has the wrong number of type arguments",
                           source->span);
                return NULL;
            }
            for (index = 0u; index < source->as.call.type_argument_count; ++index) {
                SlophCoreExpr *argument = new_expr(SLOPH_EXPR_TYPE,
                                      source->as.call.type_arguments[index]->span);
                SlophCoreExpr *application = new_expr(SLOPH_EXPR_APP, source->span);
                if (argument != NULL)
                    argument->as.type = lower_type(env,
                                         source->as.call.type_arguments[index]);
                if (argument == NULL || argument->as.type == NULL || application == NULL) {
                    sloph_core_expr_destroy(argument); sloph_core_expr_destroy(application);
                    sloph_core_expr_destroy(result); return NULL;
                }
                application->as.app.function = result;
                application->as.app.argument = argument;
                result = application;
            }
            if (source->as.call.argument_count == 0u) {
                SlophCoreExpr *unit = new_expr(SLOPH_EXPR_CON, source->span);
                SlophCoreExpr *application = new_expr(SLOPH_EXPR_APP, source->span);
                if (unit != NULL) unit->as.con.constructor = copy_text("sloph::Unit::Unit");
                if (unit == NULL || unit->as.con.constructor == NULL || application == NULL) {
                    sloph_core_expr_destroy(unit); sloph_core_expr_destroy(application);
                    sloph_core_expr_destroy(result); return NULL;
                }
                application->as.app.function = result; application->as.app.argument = unit;
                result = application;
            } else for (index = 0u; index < source->as.call.argument_count; ++index) {
                SlophCoreExpr *argument = lower_expr(env, source->as.call.arguments[index]);
                SlophCoreExpr *application = new_expr(SLOPH_EXPR_APP, source->span);
                if (argument == NULL || application == NULL) {
                    sloph_core_expr_destroy(argument); sloph_core_expr_destroy(application);
                    sloph_core_expr_destroy(result); return NULL;
                }
                application->as.app.function = result;
                application->as.app.argument = argument;
                result = application;
            }
            return result;
        }
        if ((target->kind != SLOPH_SYNTAX_EXPR_LOCAL &&
             target->kind != SLOPH_SYNTAX_EXPR_GLOBAL) ||
            (target->kind == SLOPH_SYNTAX_EXPR_LOCAL && is_local(env, target->as.name))) {
            (void)fail(env->context, "project.resolve.dynamic_call", "resolve",
                       "Source v0 calls must directly name a top-level function",
                       source->span);
            return NULL;
        }
        name = resolve_name(env, target->as.name, 1, target->span,
                            (const void **)&function);
        if (name == NULL) return NULL;
        if (function != NULL && env->module->syntax->version == 0u &&
            source->as.call.argument_count != function->parameter_count) {
            free(name);
            (void)fail(env->context, "project.resolve.call_arity", "resolve",
                       "function call has the wrong number of arguments",
                       source->span);
            return NULL;
        }
        result = new_expr(SLOPH_EXPR_GLOBAL, target->span);
        if (result == NULL) { free(name); return NULL; }
        result->as.name = name;
        for (index = 0u; index < source->as.call.type_argument_count; ++index) {
            SlophCoreExpr *argument = new_expr(SLOPH_EXPR_TYPE,
                              source->as.call.type_arguments[index]->span);
            SlophCoreExpr *application = new_expr(SLOPH_EXPR_APP, source->span);
            if (argument != NULL)
                argument->as.type = lower_type(env, source->as.call.type_arguments[index]);
            if (argument == NULL || argument->as.type == NULL || application == NULL) {
                sloph_core_expr_destroy(argument); sloph_core_expr_destroy(application);
                sloph_core_expr_destroy(result); return NULL;
            }
            application->as.app.function = result;
            application->as.app.argument = argument;
            result = application;
        }
        if (source->as.call.argument_count == 0u) {
            SlophCoreExpr *unit = new_expr(SLOPH_EXPR_CON, source->span);
            SlophCoreExpr *application = new_expr(SLOPH_EXPR_APP, source->span);
            if (unit != NULL) unit->as.con.constructor = copy_text("sloph::Unit::Unit");
            if (unit == NULL || unit->as.con.constructor == NULL || application == NULL) {
                sloph_core_expr_destroy(unit); sloph_core_expr_destroy(application);
                sloph_core_expr_destroy(result); return NULL;
            }
            application->as.app.function = result; application->as.app.argument = unit;
            result = application;
        } else for (index = 0u; index < source->as.call.argument_count; ++index) {
            SlophCoreExpr *argument = lower_expr(env, source->as.call.arguments[index]);
            SlophCoreExpr *application = new_expr(SLOPH_EXPR_APP, source->span);
            if (argument == NULL || application == NULL) {
                sloph_core_expr_destroy(argument); sloph_core_expr_destroy(application);
                sloph_core_expr_destroy(result); return NULL;
            }
            application->as.app.function = result;
            application->as.app.argument = argument;
            result = application;
        }
        return result;
    }
    if (source->kind == SLOPH_SYNTAX_EXPR_BINARY) {
        const char *name = strcmp(source->as.binary.operator_, "+") == 0 ? "core::int::add" :
                           strcmp(source->as.binary.operator_, "-") == 0 ? "core::int::sub" :
                           strcmp(source->as.binary.operator_, "*") == 0 ? "core::int::mul" :
                           strcmp(source->as.binary.operator_, "==") == 0 ? "core::int::equal" :
                           "core::int::less";
        SlophCoreExpr *global = new_expr(SLOPH_EXPR_GLOBAL, source->span);
        SlophCoreExpr *left = lower_expr(env, source->as.binary.left);
        SlophCoreExpr *right = lower_expr(env, source->as.binary.right);
        SlophCoreExpr *first = new_expr(SLOPH_EXPR_APP, source->span);
        result = new_expr(SLOPH_EXPR_APP, source->span);
        if (global != NULL) global->as.name = copy_text(name);
        if (global == NULL || global->as.name == NULL || left == NULL || right == NULL ||
            first == NULL || result == NULL) {
            sloph_core_expr_destroy(global); sloph_core_expr_destroy(left);
            sloph_core_expr_destroy(right); sloph_core_expr_destroy(first);
            sloph_core_expr_destroy(result); return NULL;
        }
        first->as.app.function = global; first->as.app.argument = left;
        result->as.app.function = first; result->as.app.argument = right;
        return result;
    }
    (void)fail(env->context, "project.lower.expression", "lower",
               "unsupported source expression", source->span);
    return NULL;
}

static SlophCoreExpr *lower_block(LowerEnv *env,
                                  const SlophSyntaxBlock *block) {
    SlophCoreExpr *body;
    size_t index;
    size_t defer_count = 0u;
    if (block == NULL) return NULL;
    LowerEnv active = *env;
    const char **locals = NULL;
    const SlophSyntaxType **local_types = NULL;
    const SlophSyntaxExpr **local_values = NULL;
    if (block->statement_count != 0u) {
        locals = calloc(env->local_count + block->statement_count, sizeof(*locals));
        local_types = calloc(env->local_count + block->statement_count,
                             sizeof(*local_types));
        local_values = calloc(env->local_count + block->statement_count,
                              sizeof(*local_values));
        if (locals == NULL || local_types == NULL || local_values == NULL) {
            free(locals); free(local_types); free(local_values); return NULL;
        }
        memcpy(locals, env->locals, env->local_count * sizeof(*locals));
        if (env->local_types != NULL)
            memcpy(local_types, env->local_types,
                   env->local_count * sizeof(*local_types));
        if (env->local_values != NULL)
            memcpy(local_values, env->local_values,
                   env->local_count * sizeof(*local_values));
        active.locals = locals; active.local_types = local_types;
        active.local_values = local_values;
        for (index = 0u; index < block->statement_count; ++index)
            if (block->statements[index].kind == SLOPH_SYNTAX_STMT_LET) {
                const SlophSyntaxStatement *statement = &block->statements[index];
                size_t position = active.local_count;
                locals[position] = statement->as.let.binder.name;
                local_types[position] = statement->as.let.binder.type;
                local_values[position] = statement->as.let.value;
                ++active.local_count;
            }
    }
    body = lower_expr(&active, block->result);
    if (body == NULL) {
        free(locals); free(local_types); free(local_values); return NULL;
    }
    for (index = 0u; index < block->statement_count; ++index)
        if (block->statements[index].kind == SLOPH_SYNTAX_STMT_DEFER) ++defer_count;
    if (defer_count != 0u) {
        SlophCoreType *result_type = infer_expr_type(&active, block->result);
        SlophCoreExpr *cleanup;
        SlophCoreExpr *outer;
        size_t defer_index = 0u;
        cleanup = new_expr(SLOPH_EXPR_LOCAL, block->span);
        if (cleanup != NULL) cleanup->as.name = copy_text("_defer_result_0");
        for (index = 0u; cleanup != NULL && index < block->statement_count; ++index) {
            const SlophSyntaxStatement *statement = &block->statements[index];
            if (statement->kind == SLOPH_SYNTAX_STMT_DEFER) {
                char name[64];
                SlophCoreExpr *let = new_expr(SLOPH_EXPR_LET, statement->span);
                (void)snprintf(name, sizeof(name), "_defer_%lu_0",
                               (unsigned long)defer_index++);
                if (let == NULL) { sloph_core_expr_destroy(cleanup); cleanup = NULL; break; }
                let->as.let.binder.name = copy_text(name);
                let->as.let.binder.mode = copy_text("own");
                let->as.let.binder.type = new_type(SLOPH_TYPE_NAMED);
                if (let->as.let.binder.type != NULL)
                    let->as.let.binder.type->as.name = copy_text("sloph::Unit");
                let->as.let.binder.span = statement->span;
                let->as.let.value = lower_expr(&active, statement->as.defer_call);
                let->as.let.body = cleanup; cleanup = let;
            }
        }
        outer = new_expr(SLOPH_EXPR_LET, block->span);
        if (outer == NULL || cleanup == NULL || result_type == NULL) {
            sloph_core_expr_destroy(outer); sloph_core_expr_destroy(cleanup);
            sloph_core_expr_destroy(body); free(locals); free(local_types);
            free(local_values); return NULL;
        }
        outer->as.let.binder.name = copy_text("_defer_result_0");
        outer->as.let.binder.mode = copy_text("own");
        outer->as.let.binder.type = result_type;
        outer->as.let.binder.span = block->span;
        outer->as.let.value = body;
        outer->as.let.body = cleanup;
        body = outer;
    }
    index = block->statement_count;
    while (index != 0u) {
        const SlophSyntaxStatement *statement = &block->statements[--index];
        SlophCoreExpr *let;
        SlophCoreExpr *value;
        if (statement->kind != SLOPH_SYNTAX_STMT_LET) continue;
        value = lower_expr(&active, statement->as.let.value);
        let = new_expr(SLOPH_EXPR_LET, statement->span);
        if (value == NULL || let == NULL) {
            sloph_core_expr_destroy(value); sloph_core_expr_destroy(let);
            sloph_core_expr_destroy(body); free(locals); free(local_types);
            free(local_values); return NULL;
        }
        let->as.let.binder.name = copy_text(statement->as.let.binder.name);
        let->as.let.binder.mode = copy_text(statement->as.let.binder.mode != NULL ?
                                            statement->as.let.binder.mode : "own");
        let->as.let.binder.type = statement->as.let.binder.type->kind ==
                                  SLOPH_SYNTAX_TYPE_INFERRED ?
                                  infer_expr_type(&active, statement->as.let.value) :
                                  lower_type(&active, statement->as.let.binder.type);
        let->as.let.binder.span = statement->as.let.binder.span;
        let->as.let.value = value;
        let->as.let.body = body;
        if (let->as.let.binder.name == NULL || let->as.let.binder.mode == NULL ||
            let->as.let.binder.type == NULL) {
            sloph_core_expr_destroy(let); free(locals); free(local_types);
            free(local_values); return NULL;
        }
        body = let;
    }
    free(locals); free(local_types); free(local_values); return body;
}

static int compare_definitions(const void *left, const void *right) {
    const SlophCoreDefinition *a = left;
    const SlophCoreDefinition *b = right;
    return strcmp(a->name, b->name);
}

static int compare_types(const void *left, const void *right) {
    const SlophCoreEnum *a = left;
    const SlophCoreEnum *b = right;
    return strcmp(a->name, b->name);
}

static int copy_json_string_array(yyjson_val *array, char ***out_items,
                                  size_t *out_count) {
    size_t index, maximum;
    yyjson_val *item;
    char **items;
    if (!yyjson_is_arr(array)) return 0;
    *out_count = yyjson_arr_size(array);
    items = *out_count != 0u ? calloc(*out_count, sizeof(*items)) : NULL;
    if (*out_count != 0u && items == NULL) return 0;
    yyjson_arr_foreach(array, index, maximum, item) {
        const char *text = yyjson_get_str(item);
        if (text == NULL || (items[index] = copy_text(text)) == NULL) return 0;
    }
    *out_items = items;
    return 1;
}

static SlophStatus enrich_foreign_bindings(SlophContext *context,
                                           const SlophProject *project,
                                           SlophCoreUnit *unit) {
    size_t provider_index, binding_index;
    for (provider_index = 0u; provider_index < project->provider_count;
         ++provider_index) {
        const SlophProjectProvider *provider = &project->providers[provider_index];
        yyjson_read_err error;
        yyjson_doc *document = yyjson_read_file(provider->bindings_path, 0u,
                                                NULL, &error);
        yyjson_val *root;
        size_t index, maximum;
        yyjson_val *item;
        if (document == NULL)
            return fail(context, "project.provider.bindings", "project",
                        "could not read validated provider bindings", (SlophSpan){0u, 0u});
        root = yyjson_doc_get_root(document);
        yyjson_arr_foreach(root, index, maximum, item) {
            const char *identity = yyjson_get_str(yyjson_obj_get(item, "identity"));
            yyjson_val *adapter_object = yyjson_obj_get(item, "adapter");
            const char *adapter = adapter_object != NULL ?
                yyjson_get_str(yyjson_obj_get(adapter_object, "kind")) : NULL;
            const char *symbol = yyjson_get_str(yyjson_obj_get(item, "symbol"));
            const char *header = yyjson_get_str(yyjson_obj_get(item, "header"));
            const char *c_result = yyjson_get_str(yyjson_obj_get(item, "c_result"));
            const char *provenance = yyjson_get_str(yyjson_obj_get(item, "provenance"));
            if (identity == NULL || adapter == NULL || symbol == NULL || header == NULL)
                continue;
            for (binding_index = 0u; binding_index < unit->foreign_binding_count;
                 ++binding_index) {
                SlophCoreForeignBinding *binding =
                    &unit->foreign_bindings[binding_index];
                if (strcmp(binding->identity, identity) != 0) continue;
                free(binding->adapter);
                binding->adapter = copy_text(adapter);
                binding->symbol = copy_text(symbol);
                binding->header = copy_text(header);
                binding->c_result = copy_text(c_result != NULL ? c_result : "void");
                binding->provenance = copy_text(provenance != NULL ? provenance : "unknown");
                if (!copy_json_string_array(yyjson_obj_get(item, "c_parameters"),
                                            &binding->c_parameters,
                                            &binding->c_parameter_count) ||
                    !copy_json_string_array(yyjson_obj_get(item, "requires"),
                                            &binding->requires,
                                            &binding->require_count) ||
                    !copy_json_string_array(yyjson_obj_get(item, "effects"),
                                            &binding->effects,
                                            &binding->effect_count)) {
                    yyjson_doc_free(document); return oom(context);
                }
                { yyjson_val *facts = yyjson_obj_get(item, "facts");
                  size_t fact_index = 0u, fact_maximum;
                  yyjson_val *key, *value;
                  binding->fact_count = yyjson_obj_size(facts);
                  if (binding->fact_count != 0u)
                      binding->facts = calloc(binding->fact_count,
                                              sizeof(*binding->facts));
                  yyjson_obj_foreach(facts, fact_index, fact_maximum, key, value) {
                      size_t position = fact_index;
                      if (position >= binding->fact_count) position = binding->fact_count - 1u;
                      binding->facts[position].key = copy_text(yyjson_get_str(key));
                      binding->facts[position].value = copy_text(yyjson_get_str(value));
                  } }
                if (binding->adapter == NULL || binding->symbol == NULL ||
                    binding->header == NULL || binding->c_result == NULL ||
                    binding->provenance == NULL) {
                    yyjson_doc_free(document); return oom(context);
                }
            }
        }
        yyjson_doc_free(document);
    }
    for (binding_index = 0u; binding_index < unit->foreign_binding_count;
         ++binding_index) {
        SlophCoreForeignBinding *binding = &unit->foreign_bindings[binding_index];
        if (binding->symbol == NULL || binding->header == NULL ||
            strcmp(binding->adapter, "direct") == 0)
            return fail(context, "project.resolve.foreign_signature", "resolve",
                        "foreign declaration has no selected provider metadata",
                        (SlophSpan){0u, 0u});
    }
    return SLOPH_STATUS_OK;
}

static SlophStatus lower_enum(LowerEnv *env, const SlophSyntaxTypeDecl *source,
                              SlophCoreEnum *out) {
    size_t i, j;
    memset(out, 0, sizeof(*out));
    out->name = qualified(env->module->name, source->name);
    out->span = source->span;
    out->owned = source->owned;
    out->type_parameter_count = source->type_parameter_count;
    out->constructor_count = source->constructor_count;
    if (out->type_parameter_count != 0u)
        out->type_parameters = calloc(out->type_parameter_count,
                                      sizeof(*out->type_parameters));
    if (out->constructor_count != 0u)
        out->constructors = calloc(out->constructor_count,
                                   sizeof(*out->constructors));
    if (out->name == NULL ||
        (out->type_parameter_count != 0u && out->type_parameters == NULL) ||
        (out->constructor_count != 0u && out->constructors == NULL))
        return oom(env->context);
    env->type_variables = (const char **)source->type_parameters;
    env->type_variable_count = source->type_parameter_count;
    for (i = 0u; i < out->type_parameter_count; ++i) {
        out->type_parameters[i] = copy_text(source->type_parameters[i]);
        if (out->type_parameters[i] == NULL) return oom(env->context);
    }
    for (i = 0u; i < out->constructor_count; ++i) {
        const SlophSyntaxConstructor *constructor = &source->constructors[i];
        SlophCoreConstructor *target = &out->constructors[i];
        target->name = qualified(out->name, constructor->name);
        target->span = constructor->span;
        target->field_count = constructor->field_count;
        if (target->field_count != 0u)
            target->fields = calloc(target->field_count, sizeof(*target->fields));
        if (target->name == NULL ||
            (target->field_count != 0u && target->fields == NULL))
            return oom(env->context);
        for (j = 0u; j < target->field_count; ++j) {
            target->fields[j].name = copy_text(constructor->fields[j].name);
            target->fields[j].type = lower_type(env, constructor->fields[j].type);
            target->fields[j].span = constructor->fields[j].span;
            if (target->fields[j].name == NULL || target->fields[j].type == NULL)
                return sloph_context_diagnostic_count(env->context) != 0u ?
                       SLOPH_STATUS_INVALID_ARGUMENT : oom(env->context);
        }
    }
    env->type_variables = NULL; env->type_variable_count = 0u;
    return SLOPH_STATUS_OK;
}

static SlophStatus lower_function(LowerEnv *env,
                                  const SlophSyntaxFunction *source,
                                  SlophCoreDefinition *out) {
    SlophCoreType *type;
    SlophCoreExpr *body;
    const char **locals;
    const SlophSyntaxType **local_types;
    size_t i;
    memset(out, 0, sizeof(*out));
    out->name = qualified(env->module->name, source->name);
    out->span = source->span;
    env->type_variables = (const char **)source->type_parameters;
    env->type_variable_count = source->type_parameter_count;
    type = lower_type(env, source->result_type);
    if (type == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
    for (i = source->parameter_count; i != 0u; --i) {
        SlophCoreType *function = new_type(SLOPH_TYPE_FUNCTION);
        if (function == NULL) { sloph_core_type_destroy(type); return oom(env->context); }
        function->as.function.mode = copy_text(source->parameters[i - 1u].mode != NULL ?
                                               source->parameters[i - 1u].mode : "own");
        function->as.function.parameter = lower_type(env, source->parameters[i - 1u].type);
        function->as.function.result = type; type = function;
    }
    if (source->parameter_count == 0u) {
        SlophCoreType *function = new_type(SLOPH_TYPE_FUNCTION);
        if (function == NULL) { sloph_core_type_destroy(type); return oom(env->context); }
        function->as.function.mode = copy_text("own");
        function->as.function.parameter = new_type(SLOPH_TYPE_NAMED);
        if (function->as.function.parameter != NULL)
            function->as.function.parameter->as.name = copy_text("sloph::Unit");
        function->as.function.result = type; type = function;
    }
    for (i = source->type_parameter_count; i != 0u; --i) {
        SlophCoreType *forall = new_type(SLOPH_TYPE_FORALL);
        if (forall == NULL) { sloph_core_type_destroy(type); return oom(env->context); }
        forall->as.forall.parameter = copy_text(source->type_parameters[i - 1u]);
        forall->as.forall.body = type; type = forall;
    }
    out->type = type;
    locals = calloc(source->parameter_count + 1u, sizeof(*locals));
    local_types = calloc(source->parameter_count + 1u, sizeof(*local_types));
    if (locals == NULL || local_types == NULL) {
        free(locals); free(local_types); return oom(env->context);
    }
    for (i = 0u; i < source->parameter_count; ++i) {
        locals[i] = source->parameters[i].name;
        local_types[i] = source->parameters[i].type;
    }
    env->locals = locals; env->local_types = local_types;
    env->local_count = source->parameter_count;
    if (source->kind == SLOPH_SYNTAX_FUNCTION_DEFINED) {
        body = lower_block(env, source->body);
    } else {
        body = new_expr(SLOPH_EXPR_PRIM, source->span);
        if (body != NULL) {
            body->as.prim.name = copy_text(source->binding);
            body->as.prim.count = source->parameter_count;
            if (body->as.prim.count != 0u)
                body->as.prim.items = calloc(body->as.prim.count,
                                             sizeof(*body->as.prim.items));
            for (i = 0u; i < body->as.prim.count && body->as.prim.items != NULL; ++i) {
                body->as.prim.items[i] = new_expr(SLOPH_EXPR_LOCAL,
                                                  source->parameters[i].span);
                if (body->as.prim.items[i] != NULL)
                    body->as.prim.items[i]->as.name = copy_text(source->parameters[i].name);
            }
        }
    }
    for (i = source->parameter_count; body != NULL && i != 0u; --i) {
        SlophCoreExpr *lambda = new_expr(SLOPH_EXPR_LAM, source->span);
        if (lambda == NULL) { sloph_core_expr_destroy(body); body = NULL; break; }
        lambda->as.lam.binder.name = copy_text(source->parameters[i - 1u].name);
        lambda->as.lam.binder.mode = copy_text(source->parameters[i - 1u].mode != NULL ?
                                               source->parameters[i - 1u].mode : "own");
        lambda->as.lam.binder.type = lower_type(env, source->parameters[i - 1u].type);
        lambda->as.lam.binder.span = source->parameters[i - 1u].span;
        lambda->as.lam.body = body; body = lambda;
    }
    if (body != NULL && source->parameter_count == 0u) {
        SlophCoreExpr *lambda = new_expr(SLOPH_EXPR_LAM, source->span);
        if (lambda != NULL) {
            lambda->as.lam.binder.name = copy_text("_unit");
            lambda->as.lam.binder.mode = copy_text("own");
            lambda->as.lam.binder.type = new_type(SLOPH_TYPE_NAMED);
            if (lambda->as.lam.binder.type != NULL)
                lambda->as.lam.binder.type->as.name = copy_text("sloph::Unit");
            lambda->as.lam.body = body; body = lambda;
        }
    }
    for (i = source->type_parameter_count; body != NULL && i != 0u; --i) {
        SlophCoreExpr *lambda = new_expr(SLOPH_EXPR_LAM, source->span);
        if (lambda == NULL) { sloph_core_expr_destroy(body); body = NULL; break; }
        lambda->as.lam.binder.name = copy_text(source->type_parameters[i - 1u]);
        lambda->as.lam.binder.is_type = 1;
        lambda->as.lam.binder.span = source->span;
        lambda->as.lam.body = body; body = lambda;
    }
    free(locals); free(local_types);
    env->locals = NULL; env->local_types = NULL; env->local_count = 0u;
    env->type_variables = NULL; env->type_variable_count = 0u;
    out->value = body;
    if (body != NULL && out->name != NULL) return SLOPH_STATUS_OK;
    return sloph_context_diagnostic_count(env->context) != 0u ?
           SLOPH_STATUS_INVALID_ARGUMENT : oom(env->context);
}

static SlophStatus validate_imports(SlophContext *context,
                                    const SlophProject *project) {
    size_t i, j, k;
    for (i = 0u; i < project->module_count; ++i) {
        const SlophProjectModule *module = &project->modules[i];
        for (j = 0u; j < module->syntax->type_count; ++j) {
            const SlophSyntaxTypeDecl *type = &module->syntax->types[j];
            if (type->kind == SLOPH_SYNTAX_TYPE_DECL_INTRINSIC &&
                (!module->bundled || strcmp(module->name, "core") != 0 ||
                 (strcmp(type->name, "Int") != 0 && strcmp(type->name, "Bytes") != 0))) {
                char details[512], message[512];
                (void)snprintf(details, sizeof(details),
                               "{\"module\":\"%s\",\"type\":\"%s\"}",
                               module->name, type->name);
                (void)snprintf(message, sizeof(message),
                               "intrinsic types are restricted to the canonical core package");
                (void)sloph_context_add_diagnostic_full(context,
                    "project.resolve.trusted_intrinsic", "resolve", message,
                    details, type->span, SLOPH_SEVERITY_ERROR);
                return SLOPH_STATUS_INVALID_ARGUMENT;
            }
        }
        for (j = 0u; j < module->syntax->function_count; ++j) {
            const SlophSyntaxFunction *function = &module->syntax->functions[j];
            if (function->kind == SLOPH_SYNTAX_FUNCTION_INTRINSIC &&
                (!module->bundled || strncmp(module->name, "core::", 6u) != 0)) {
                char details[512];
                (void)snprintf(details, sizeof(details),
                               "{\"function\":\"%s\",\"module\":\"%s\"}",
                               function->name, module->name);
                (void)sloph_context_add_diagnostic_full(context,
                    "project.resolve.trusted_intrinsic", "resolve",
                    "intrinsic functions are restricted to canonical core modules",
                    details, function->span, SLOPH_SEVERITY_ERROR);
                return SLOPH_STATUS_INVALID_ARGUMENT;
            }
        }
        for (j = 0u; j < module->syntax->import_count; ++j) {
            const SlophSyntaxImport *import_ = &module->syntax->imports[j];
            const SlophSyntaxDirectImport *direct;
            const SlophProjectModule *target;
            if (import_->kind != SLOPH_SYNTAX_IMPORT_DIRECT) continue;
            direct = &import_->as.direct;
            target = find_module(project, direct->module);
            if (target == NULL) continue; /* Project loading diagnoses this. */
            for (k = 0u; k < direct->name_count; ++k) {
                int public_, kind;
                const void *declaration;
                const SlophProjectModule *owner = target;
                int direct_declaration = module_decl(target, direct->names[k],
                                                     &public_, &kind,
                                                     &declaration);
                if (!direct_declaration &&
                    !module_export(project, target, direct->names[k], &public_,
                                   &kind, &declaration, &owner))
                    return fail(context, "project.resolve.unknown_import", "resolve",
                                "imported module has no such declaration",
                                direct->span);
                if (!public_) {
                    char *identity = qualified(target->name, direct->names[k]);
                    char details[512], message[512];
                    if (identity == NULL) return oom(context);
                    (void)snprintf(details, sizeof(details),
                                   "{\"declaration\":\"%s\"}", identity);
                    (void)snprintf(message, sizeof(message),
                                   "declaration '%s' is private", identity);
                    free(identity);
                    (void)sloph_context_add_diagnostic_full(context,
                        "project.resolve.private_import", "resolve", message,
                        details, direct->span, SLOPH_SEVERITY_ERROR);
                    return SLOPH_STATUS_INVALID_ARGUMENT;
                }
                (void)kind; (void)declaration;
            }
        }
    }
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_project_elaborate(SlophContext *context,
                                    const SlophProject *project,
                                    SlophCoreUnit **out_unit) {
    SlophCoreUnit *unit;
    size_t module_index, definition_index = 0u, type_index = 0u;
    size_t count = 0u, type_count = 0u, foreign_count = 0u, foreign_index = 0u;
    if (context == NULL || project == NULL || out_unit == NULL)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    *out_unit = NULL;
    if (validate_imports(context, project) != SLOPH_STATUS_OK)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    for (module_index = 0u; module_index < project->module_count; ++module_index) {
        size_t i;
        count += project->modules[module_index].syntax->value_count +
                 project->modules[module_index].syntax->function_count;
        for (i = 0u; i < project->modules[module_index].syntax->function_count; ++i)
            if (project->modules[module_index].syntax->functions[i].kind ==
                SLOPH_SYNTAX_FUNCTION_FOREIGN) ++foreign_count;
        for (i = 0u; i < project->modules[module_index].syntax->type_count; ++i)
            if (project->modules[module_index].syntax->types[i].kind ==
                SLOPH_SYNTAX_TYPE_DECL_ENUM) ++type_count;
    }
    unit = calloc(1u, sizeof(*unit));
    if (unit == NULL) return oom(context);
    unit->version = project->module_count != 0u ?
                    (int)project->modules[project->module_count - 1u].syntax->version : 0;
    if (unit->version == 1) unit->version = 2;
    if (unit->version == 2) {
        for (module_index = 0u; module_index < project->module_count; ++module_index) {
            size_t i;
            for (i = 0u; i < project->modules[module_index].syntax->type_count; ++i)
                if (project->modules[module_index].syntax->types[i].owned)
                    unit->version = 3;
        }
    }
    unit->type_count = type_count;
    unit->foreign_binding_count = foreign_count;
    if (type_count != 0u) unit->types = calloc(type_count, sizeof(*unit->types));
    if (foreign_count != 0u)
        unit->foreign_bindings = calloc(foreign_count,
                                        sizeof(*unit->foreign_bindings));
    unit->definition_count = count;
    if (count != 0u) unit->definitions = calloc(count, sizeof(*unit->definitions));
    if ((count != 0u && unit->definitions == NULL) ||
        (type_count != 0u && unit->types == NULL) ||
        (foreign_count != 0u && unit->foreign_bindings == NULL)) {
        sloph_core_free(unit); return oom(context);
    }
    for (module_index = 0u; module_index < project->module_count; ++module_index) {
        const SlophProjectModule *module = &project->modules[module_index];
        LowerEnv env;
        size_t value_index;
        memset(&env, 0, sizeof(env));
        env.context = context;
        env.project = project;
        env.module = module;
        for (value_index = 0u; value_index < module->syntax->function_count;
             ++value_index) {
            const SlophSyntaxFunction *function =
                &module->syntax->functions[value_index];
            SlophCoreForeignBinding *binding;
            size_t parameter_index;
            if (function->kind != SLOPH_SYNTAX_FUNCTION_FOREIGN) continue;
            binding = &unit->foreign_bindings[foreign_index++];
            binding->identity = copy_text(function->binding);
            binding->adapter = copy_text("direct");
            binding->provider = copy_text(module->name);
            binding->parameter_count = function->parameter_count;
            if (binding->parameter_count != 0u)
                binding->parameters = calloc(binding->parameter_count,
                                              sizeof(*binding->parameters));
            for (parameter_index = 0u;
                 parameter_index < binding->parameter_count; ++parameter_index)
                binding->parameters[parameter_index] = lower_type(&env,
                                      function->parameters[parameter_index].type);
            binding->result = lower_type(&env, function->result_type);
            if (binding->identity == NULL || binding->adapter == NULL ||
                binding->provider == NULL || binding->result == NULL) {
                sloph_core_free(unit); return oom(context);
            }
        }
        for (value_index = 0u; value_index < module->syntax->type_count;
             ++value_index) {
            if (module->syntax->types[value_index].kind !=
                SLOPH_SYNTAX_TYPE_DECL_ENUM) continue;
            if (lower_enum(&env, &module->syntax->types[value_index],
                           &unit->types[type_index++]) != SLOPH_STATUS_OK) {
                sloph_core_free(unit); return SLOPH_STATUS_INVALID_ARGUMENT;
            }
        }
        for (value_index = 0u; value_index < module->syntax->function_count;
             ++value_index) {
            if (lower_function(&env, &module->syntax->functions[value_index],
                               &unit->definitions[definition_index++]) !=
                SLOPH_STATUS_OK) {
                sloph_core_free(unit); return SLOPH_STATUS_INVALID_ARGUMENT;
            }
        }
        for (value_index = 0u; value_index < module->syntax->value_count;
             ++value_index) {
            const SlophSyntaxValue *source = &module->syntax->values[value_index];
            SlophCoreDefinition *definition = &unit->definitions[definition_index++];
            size_t module_size = strlen(module->name), name_size = strlen(source->name);
            definition->name = malloc(module_size + name_size + 3u);
            if (definition->name != NULL) {
                memcpy(definition->name, module->name, module_size);
                memcpy(definition->name + module_size, "::", 2u);
                memcpy(definition->name + module_size + 2u, source->name,
                       name_size + 1u);
            }
            definition->type = lower_type(&env, source->type);
            definition->value = lower_block(&env, source->value);
            definition->span = source->span;
            if (definition->name == NULL || definition->type == NULL ||
                definition->value == NULL) {
                sloph_core_free(unit);
                return sloph_context_diagnostic_count(context) != 0u ?
                       SLOPH_STATUS_INVALID_ARGUMENT : oom(context);
            }
        }
    }
    qsort(unit->types, unit->type_count, sizeof(*unit->types), compare_types);
    qsort(unit->definitions, unit->definition_count,
          sizeof(*unit->definitions), compare_definitions);
    if (enrich_foreign_bindings(context, project, unit) != SLOPH_STATUS_OK) {
        sloph_core_free(unit); return SLOPH_STATUS_INVALID_ARGUMENT;
    }
    if (sloph_core_validate(context, unit) != SLOPH_STATUS_OK) {
        sloph_core_free(unit); return SLOPH_STATUS_INVALID_ARGUMENT;
    }
    for (definition_index = 0u; definition_index < unit->definition_count;
         ++definition_index) {
        if (strcmp(unit->definitions[definition_index].name,
                   project->manifest.entry) == 0) break;
    }
    if (definition_index == unit->definition_count) {
        SlophSpan span = {0u, 0u};
        sloph_core_free(unit);
        return fail(context, "project.entry.missing", "resolve",
                    "entry does not name a project value", span);
    }
    if (sloph_core_adopt_allocator(context, &unit) != SLOPH_STATUS_OK) {
        sloph_core_free(unit);
        return oom(context);
    }
    *out_unit = unit;
    return SLOPH_STATUS_OK;
}
