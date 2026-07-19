#include "core_internal.h"
#include "sloph/context.h"

#include <stdlib.h>
#include <string.h>

static void string_array_destroy(char **items, size_t count) {
    size_t i;
    for (i = 0; i < count; ++i) free(items[i]);
    free(items);
}

void sloph_core_type_destroy(SlophCoreType *type) {
    size_t i;
    if (type == NULL) return;
    switch (type->kind) {
    case SLOPH_TYPE_NAMED:
    case SLOPH_TYPE_VARIABLE:
        free(type->as.name);
        break;
    case SLOPH_TYPE_APPLIED:
        free(type->as.applied.constructor);
        for (i = 0; i < type->as.applied.count; ++i)
            sloph_core_type_destroy(type->as.applied.items[i]);
        free(type->as.applied.items);
        break;
    case SLOPH_TYPE_FUNCTION:
        free(type->as.function.mode);
        sloph_core_type_destroy(type->as.function.parameter);
        sloph_core_type_destroy(type->as.function.result);
        break;
    case SLOPH_TYPE_FORALL:
        free(type->as.forall.parameter);
        sloph_core_type_destroy(type->as.forall.body);
        break;
    case SLOPH_TYPE_INT:
    case SLOPH_TYPE_BYTES:
        break;
    }
    free(type);
}

static void binder_destroy(SlophCoreBinder *binder) {
    free(binder->name);
    free(binder->mode);
    sloph_core_type_destroy(binder->type);
}

void sloph_core_expr_destroy(SlophCoreExpr *expression) {
    size_t i;
    if (expression == NULL) return;
    switch (expression->kind) {
    case SLOPH_EXPR_INT: free(expression->as.integer); break;
    case SLOPH_EXPR_BYTES: free(expression->as.bytes.data); break;
    case SLOPH_EXPR_LOCAL:
    case SLOPH_EXPR_GLOBAL: free(expression->as.name); break;
    case SLOPH_EXPR_TYPE: sloph_core_type_destroy(expression->as.type); break;
    case SLOPH_EXPR_LAM:
        binder_destroy(&expression->as.lam.binder);
        sloph_core_expr_destroy(expression->as.lam.body);
        break;
    case SLOPH_EXPR_APP:
        sloph_core_expr_destroy(expression->as.app.function);
        sloph_core_expr_destroy(expression->as.app.argument);
        break;
    case SLOPH_EXPR_LET:
        binder_destroy(&expression->as.let.binder);
        sloph_core_expr_destroy(expression->as.let.value);
        sloph_core_expr_destroy(expression->as.let.body);
        break;
    case SLOPH_EXPR_PRIM:
        free(expression->as.prim.name);
        for (i = 0; i < expression->as.prim.count; ++i)
            sloph_core_expr_destroy(expression->as.prim.items[i]);
        free(expression->as.prim.items);
        break;
    case SLOPH_EXPR_CON:
        free(expression->as.con.constructor);
        for (i = 0; i < expression->as.con.type_argument_count; ++i)
            sloph_core_type_destroy(expression->as.con.type_arguments[i]);
        free(expression->as.con.type_arguments);
        for (i = 0; i < expression->as.con.field_count; ++i)
            sloph_core_expr_destroy(expression->as.con.fields[i]);
        free(expression->as.con.fields);
        break;
    case SLOPH_EXPR_CASE:
        sloph_core_expr_destroy(expression->as.case_.scrutinee);
        sloph_core_type_destroy(expression->as.case_.result_type);
        for (i = 0; i < expression->as.case_.alternative_count; ++i) {
            SlophCoreAlternative *alt = &expression->as.case_.alternatives[i];
            size_t j;
            free(alt->constructor);
            for (j = 0; j < alt->binder_count; ++j) binder_destroy(&alt->binders[j]);
            free(alt->binders);
            sloph_core_expr_destroy(alt->body);
        }
        free(expression->as.case_.alternatives);
        break;
    }
    free(expression);
}

void sloph_core_free(SlophCoreUnit *unit) {
    size_t i, j, k;
    SlophAllocator allocator;
    if (unit == NULL) return;
    allocator = unit->allocator;
    for (i = 0; i < unit->type_count; ++i) {
        SlophCoreEnum *type = &unit->types[i];
        free(type->name);
        string_array_destroy(type->type_parameters, type->type_parameter_count);
        for (j = 0; j < type->constructor_count; ++j) {
            SlophCoreConstructor *ctor = &type->constructors[j];
            free(ctor->name);
            for (k = 0; k < ctor->field_count; ++k) {
                free(ctor->fields[k].name);
                sloph_core_type_destroy(ctor->fields[k].type);
            }
            free(ctor->fields);
        }
        free(type->constructors);
    }
    free(unit->types);
    for (i = 0; i < unit->definition_count; ++i) {
        free(unit->definitions[i].name);
        sloph_core_type_destroy(unit->definitions[i].type);
        sloph_core_expr_destroy(unit->definitions[i].value);
    }
    free(unit->definitions);
    for (i = 0; i < unit->foreign_binding_count; ++i) {
        SlophCoreForeignBinding *binding = &unit->foreign_bindings[i];
        free(binding->identity); free(binding->symbol); free(binding->adapter);
        for (j = 0; j < binding->parameter_count; ++j)
            sloph_core_type_destroy(binding->parameters[j]);
        free(binding->parameters); sloph_core_type_destroy(binding->result);
        string_array_destroy(binding->c_parameters, binding->c_parameter_count);
        free(binding->c_result); free(binding->provider); free(binding->header);
        string_array_destroy(binding->requires, binding->require_count);
        string_array_destroy(binding->effects, binding->effect_count);
        for (j = 0; j < binding->fact_count; ++j) {
            free(binding->facts[j].key); free(binding->facts[j].value);
        }
        free(binding->facts); free(binding->provenance);
    }
    free(unit->foreign_bindings);
    if (allocator.deallocate != NULL)
        allocator.deallocate(allocator.user_data, unit, sizeof(*unit));
    else
        free(unit);
}

SlophStatus sloph_core_adopt_allocator(SlophContext *context,
                                       SlophCoreUnit **unit) {
    const SlophAllocator *allocator;
    SlophCoreUnit *adopted;
    if (context == NULL || unit == NULL || *unit == NULL)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    allocator = sloph_context_allocator(context);
    adopted = allocator->allocate(allocator->user_data, sizeof(*adopted));
    if (adopted == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    memcpy(adopted, *unit, sizeof(*adopted));
    adopted->allocator = *allocator;
    free(*unit);
    *unit = adopted;
    return SLOPH_STATUS_OK;
}
