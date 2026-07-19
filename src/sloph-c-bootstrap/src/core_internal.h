#ifndef SLOPH_CORE_INTERNAL_H
#define SLOPH_CORE_INTERNAL_H

#include "sloph/core.h"

#include <stddef.h>

typedef SlophSpan SlophCoreSpan;

typedef enum {
    SLOPH_TYPE_INT,
    SLOPH_TYPE_BYTES,
    SLOPH_TYPE_NAMED,
    SLOPH_TYPE_VARIABLE,
    SLOPH_TYPE_APPLIED,
    SLOPH_TYPE_FUNCTION,
    SLOPH_TYPE_FORALL
} SlophCoreTypeKind;

typedef struct SlophCoreType SlophCoreType;
struct SlophCoreType {
    SlophCoreTypeKind kind;
    union {
        char *name;
        struct {
            char *constructor;
            SlophCoreType **items;
            size_t count;
        } applied;
        struct {
            char *mode;
            SlophCoreType *parameter;
            SlophCoreType *result;
        } function;
        struct {
            char *parameter;
            SlophCoreType *body;
        } forall;
    } as;
};

typedef struct {
    char *name;
    SlophCoreType *type; /* NULL for type binders. */
    SlophCoreSpan span;
    char *mode;          /* NULL for type binders, otherwise own/borrow. */
    int is_type;
} SlophCoreBinder;

typedef enum {
    SLOPH_EXPR_INT,
    SLOPH_EXPR_BYTES,
    SLOPH_EXPR_LOCAL,
    SLOPH_EXPR_GLOBAL,
    SLOPH_EXPR_TYPE,
    SLOPH_EXPR_LAM,
    SLOPH_EXPR_APP,
    SLOPH_EXPR_LET,
    SLOPH_EXPR_PRIM,
    SLOPH_EXPR_CON,
    SLOPH_EXPR_CASE
} SlophCoreExprKind;

typedef struct SlophCoreExpr SlophCoreExpr;

typedef struct {
    char *constructor;
    SlophCoreBinder *binders;
    size_t binder_count;
    SlophCoreExpr *body;
    SlophCoreSpan span;
} SlophCoreAlternative;

struct SlophCoreExpr {
    SlophCoreExprKind kind;
    SlophCoreSpan span;
    union {
        char *integer; /* canonical signed decimal; -0 is normalized to 0 */
        struct { unsigned char *data; size_t length; } bytes;
        char *name;
        SlophCoreType *type;
        struct { SlophCoreBinder binder; SlophCoreExpr *body; } lam;
        struct { SlophCoreExpr *function; SlophCoreExpr *argument; } app;
        struct {
            SlophCoreBinder binder;
            SlophCoreExpr *value;
            SlophCoreExpr *body;
        } let;
        struct { char *name; SlophCoreExpr **items; size_t count; } prim;
        struct {
            char *constructor;
            SlophCoreType **type_arguments;
            size_t type_argument_count;
            SlophCoreExpr **fields;
            size_t field_count;
        } con;
        struct {
            SlophCoreExpr *scrutinee;
            SlophCoreType *result_type;
            SlophCoreAlternative *alternatives;
            size_t alternative_count;
        } case_;
    } as;
};

typedef struct {
    char *name;
    SlophCoreType *type;
    SlophCoreSpan span;
} SlophCoreField;

typedef struct {
    char *name;
    SlophCoreField *fields;
    size_t field_count;
    SlophCoreSpan span;
} SlophCoreConstructor;

typedef struct {
    char *name;
    char **type_parameters;
    size_t type_parameter_count;
    SlophCoreConstructor *constructors;
    size_t constructor_count;
    SlophCoreSpan span;
    int owned;
} SlophCoreEnum;

typedef struct {
    char *name;
    SlophCoreType *type;
    SlophCoreExpr *value;
    SlophCoreSpan span;
} SlophCoreDefinition;

typedef struct { char *key; char *value; } SlophCoreFact;

typedef struct {
    char *identity;
    char *symbol;
    char *adapter;
    SlophCoreType **parameters;
    size_t parameter_count;
    SlophCoreType *result;
    char **c_parameters;
    size_t c_parameter_count;
    char *c_result;
    char *provider;
    char *header;
    char **requires;
    size_t require_count;
    char **effects;
    size_t effect_count;
    SlophCoreFact *facts;
    size_t fact_count;
    char *provenance;
} SlophCoreForeignBinding;

struct SlophCoreUnit {
    int version;
    SlophCoreEnum *types;
    size_t type_count;
    SlophCoreDefinition *definitions;
    size_t definition_count;
    SlophCoreForeignBinding *foreign_bindings;
    size_t foreign_binding_count;
    SlophCoreSpan span;
};

void sloph_core_type_destroy(SlophCoreType *type);
void sloph_core_expr_destroy(SlophCoreExpr *expression);

/* Implemented by the validation/evaluation slice. */
SlophStatus sloph_core_validate(SlophContext *context, SlophCoreUnit *unit);

#endif
