#ifndef SLOPH_SYNTAX_INTERNAL_H
#define SLOPH_SYNTAX_INTERNAL_H

#include "internal.h"
#include "sloph/syntax.h"

#include <stdbool.h>

typedef enum SlophSyntaxTypeKind {
    SLOPH_SYNTAX_TYPE_INT, SLOPH_SYNTAX_TYPE_NAMED,
    SLOPH_SYNTAX_TYPE_APPLIED, SLOPH_SYNTAX_TYPE_FUNCTION,
    SLOPH_SYNTAX_TYPE_INFERRED
} SlophSyntaxTypeKind;
typedef struct SlophSyntaxType SlophSyntaxType;
struct SlophSyntaxType {
    SlophSyntaxTypeKind kind; SlophSpan span;
    union {
        char *name;
        struct { char *constructor; SlophSyntaxType **items; size_t count; } applied;
        struct { char *mode; SlophSyntaxType *parameter; SlophSyntaxType *result; } function;
    } as;
};

typedef struct SlophSyntaxBinder {
    char *name; SlophSyntaxType *type; SlophSpan span; char *mode;
} SlophSyntaxBinder;

typedef enum SlophSyntaxExprKind {
    SLOPH_SYNTAX_EXPR_INT, SLOPH_SYNTAX_EXPR_BYTES,
    SLOPH_SYNTAX_EXPR_LOCAL, SLOPH_SYNTAX_EXPR_GLOBAL,
    SLOPH_SYNTAX_EXPR_CALL, SLOPH_SYNTAX_EXPR_BINARY,
    SLOPH_SYNTAX_EXPR_LAMBDA, SLOPH_SYNTAX_EXPR_IF,
    SLOPH_SYNTAX_EXPR_CONSTRUCTOR, SLOPH_SYNTAX_EXPR_PRIMITIVE,
    SLOPH_SYNTAX_EXPR_CASE
} SlophSyntaxExprKind;
typedef struct SlophSyntaxExpr SlophSyntaxExpr;
typedef struct SlophSyntaxBlock SlophSyntaxBlock;
typedef struct SlophSyntaxCaseAlternative {
    char *constructor; SlophSyntaxBinder *binders; size_t binder_count;
    SlophSyntaxBlock *body; SlophSpan span;
} SlophSyntaxCaseAlternative;
struct SlophSyntaxExpr {
    SlophSyntaxExprKind kind; SlophSpan span;
    union {
        char *integer;
        struct { unsigned char *data; size_t length; } bytes;
        char *name;
        struct { SlophSyntaxExpr *function; SlophSyntaxExpr **arguments; size_t argument_count; SlophSyntaxType **type_arguments; size_t type_argument_count; } call;
        struct { char *operator_; SlophSyntaxExpr *left; SlophSyntaxExpr *right; } binary;
        struct { SlophSyntaxBinder *parameters; size_t parameter_count; SlophSyntaxType *result_type; SlophSyntaxBlock *body; } lambda;
        struct { SlophSyntaxExpr *condition; SlophSyntaxBlock *then_body; SlophSyntaxBlock *else_body; } if_;
        struct { char *constructor; SlophSyntaxExpr **arguments; size_t argument_count; SlophSyntaxType **type_arguments; size_t type_argument_count; } constructor;
        struct { char *name; SlophSyntaxExpr **arguments; size_t argument_count; } primitive;
        struct { SlophSyntaxExpr *scrutinee; SlophSyntaxType *result_type; SlophSyntaxCaseAlternative *alternatives; size_t alternative_count; } case_;
    } as;
};

typedef enum SlophSyntaxStatementKind { SLOPH_SYNTAX_STMT_LET, SLOPH_SYNTAX_STMT_DEFER } SlophSyntaxStatementKind;
typedef struct SlophSyntaxStatement {
    SlophSyntaxStatementKind kind; SlophSpan span; bool propagation;
    union { struct { SlophSyntaxBinder binder; SlophSyntaxExpr *value; } let; SlophSyntaxExpr *defer_call; } as;
} SlophSyntaxStatement;
struct SlophSyntaxBlock { SlophSyntaxStatement *statements; size_t statement_count; SlophSyntaxExpr *result; SlophSpan span; };

typedef enum SlophSyntaxPatternKind { SLOPH_SYNTAX_PATTERN_CONSTANT, SLOPH_SYNTAX_PATTERN_TUPLE } SlophSyntaxPatternKind;
typedef struct SlophSyntaxPattern SlophSyntaxPattern;
struct SlophSyntaxPattern { SlophSyntaxPatternKind kind; SlophSpan span; union { char *constant; struct { SlophSyntaxPattern **items; size_t count; } tuple; } as; };
typedef struct SlophSyntaxAvailability { char *selector; SlophSyntaxPattern *pattern; SlophSpan span; } SlophSyntaxAvailability;

typedef struct SlophSyntaxDirectImport { char *module; char **names; size_t name_count; bool public_; SlophSpan span; } SlophSyntaxDirectImport;
typedef struct SlophSyntaxConditionalAlternative { SlophSyntaxPattern *pattern; SlophSyntaxDirectImport import_; SlophSpan span; } SlophSyntaxConditionalAlternative;
typedef enum SlophSyntaxImportKind { SLOPH_SYNTAX_IMPORT_DIRECT, SLOPH_SYNTAX_IMPORT_CONDITIONAL } SlophSyntaxImportKind;
typedef struct SlophSyntaxImport { SlophSyntaxImportKind kind; SlophSpan span; union { SlophSyntaxDirectImport direct; struct { char *selector; SlophSyntaxConditionalAlternative *alternatives; size_t alternative_count; } conditional; } as; } SlophSyntaxImport;

typedef struct SlophSyntaxField { char *name; SlophSyntaxType *type; SlophSpan span; } SlophSyntaxField;
typedef struct SlophSyntaxConstructor { char *name; SlophSyntaxField *fields; size_t field_count; SlophSpan span; } SlophSyntaxConstructor;
typedef enum SlophSyntaxTypeDeclKind { SLOPH_SYNTAX_TYPE_DECL_ENUM, SLOPH_SYNTAX_TYPE_DECL_INTRINSIC } SlophSyntaxTypeDeclKind;
typedef struct SlophSyntaxTypeDecl { SlophSyntaxTypeDeclKind kind; char *name; bool public_; bool owned; char **type_parameters; size_t type_parameter_count; SlophSyntaxConstructor *constructors; size_t constructor_count; SlophSpan span; } SlophSyntaxTypeDecl;

typedef enum SlophSyntaxFunctionKind { SLOPH_SYNTAX_FUNCTION_DEFINED, SLOPH_SYNTAX_FUNCTION_INTRINSIC, SLOPH_SYNTAX_FUNCTION_FOREIGN } SlophSyntaxFunctionKind;
typedef struct SlophSyntaxFunction { SlophSyntaxFunctionKind kind; char *name; SlophSyntaxBinder *parameters; size_t parameter_count; SlophSyntaxType *result_type; SlophSyntaxBlock *body; char *binding; bool public_; char **type_parameters; size_t type_parameter_count; SlophSpan span; } SlophSyntaxFunction;
typedef struct SlophSyntaxValue { char *name; SlophSyntaxType *type; SlophSyntaxBlock *value; bool public_; SlophSpan span; } SlophSyntaxValue;

struct SlophSyntaxModule {
    SlophContext *context; SlophArena arena; unsigned version; size_t node_count;
    char *name; SlophSyntaxImport *imports; size_t import_count;
    SlophSyntaxTypeDecl *types; size_t type_count;
    SlophSyntaxFunction *functions; size_t function_count;
    SlophSyntaxValue *values; size_t value_count;
    SlophSyntaxAvailability *availability; SlophSpan span;
};

SlophStatus sloph_syntax_new_module(SlophContext *, unsigned, SlophSyntaxModule **);
SlophStatus sloph_syntax_alloc(SlophSyntaxModule *, size_t, size_t, void **);
SlophStatus sloph_syntax_string(SlophSyntaxModule *, const char *, size_t, char **);
SlophStatus sloph_syntax_diagnostic(SlophContext *, const char *, const char *, const char *, SlophSpan, SlophStatus);
bool sloph_syntax_standard_transform(const char *name, bool *out_swap_branches);

#endif
