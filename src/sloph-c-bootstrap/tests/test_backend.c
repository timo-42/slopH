#include "sloph/sloph.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static SlophContext *new_context(void) {
    SlophContext *context = NULL;
    assert(sloph_context_create(NULL, &context) == SLOPH_STATUS_OK);
    return context;
}

static void test_deterministic_heartwood_to_timber(void) {
    static const unsigned char source[] =
        "(core 0 (types (enum example::Choice "
        "(ctor example::Choice::None) "
        "(ctor example::Choice::Some (field value Int)))) "
        "(defs (def example::stored (named example::Choice) "
        "(con example::Choice::Some (int 41))) "
        "(def example::main Int (case (global example::stored) Int "
        "(alt example::Choice::None (int 0)) "
        "(alt example::Choice::Some (bind x Int) "
        "(prim int.add (local x) (int 1)))))))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    char *first = NULL, *second = NULL;
    size_t first_length = 0u, second_length = 0u;
    assert(sloph_core_parse(context, source, sizeof(source) - 1u, &unit) ==
           SLOPH_STATUS_OK);
    assert(sloph_heartwood_to_timber(context, unit, "example::main", &first,
                                     &first_length) == SLOPH_STATUS_OK);
    assert(sloph_backend_emit_c11(context, unit, "example::main", &second,
                                  &second_length) == SLOPH_STATUS_OK);
    assert(first_length == second_length);
    assert(memcmp(first, second, first_length + 1u) == 0);
    assert(strstr(first, "#include <stdint.h>") != NULL);
    assert(strstr(first, "sl_int_add") != NULL);
    assert(strstr(first, "int main(void)") != NULL);
    assert(strstr(first, "example::Choice::Some") != NULL);
    free(first); free(second);
    sloph_core_free(unit); sloph_context_destroy(context);
}

static void test_unknown_entry_is_structured(void) {
    static const unsigned char source[] =
        "(core 0 (types) (defs (def example::main Int (int 0))))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    SlophDiagnosticView diagnostic;
    char *output = NULL;
    size_t length = 0u;
    assert(sloph_core_parse(context, source, sizeof(source) - 1u, &unit) ==
           SLOPH_STATUS_OK);
    assert(sloph_heartwood_to_timber(context, unit, "example::missing",
                                     &output, &length) ==
           SLOPH_STATUS_INVALID_ARGUMENT);
    assert(output == NULL && length == 0u);
    assert(sloph_context_diagnostic(context,
           sloph_context_diagnostic_count(context) - 1u, &diagnostic) ==
           SLOPH_STATUS_OK);
    assert(strcmp(diagnostic.code, "backend.c11.unknown_symbol") == 0);
    assert(strcmp(diagnostic.phase, "backend") == 0);
    sloph_core_free(unit); sloph_context_destroy(context);
}

static void test_function_entry_rejection_is_structured(void) {
    static const unsigned char source[] =
        "(core 0 (types) (defs (def example::identity (fn Int Int) "
        "(lam (bind x Int) (local x)))))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    SlophDiagnosticView diagnostic;
    char *output = NULL;
    size_t length = 0u;
    assert(sloph_core_parse(context, source, sizeof(source) - 1u, &unit) ==
           SLOPH_STATUS_OK);
    assert(sloph_heartwood_to_timber(context, unit, "example::identity",
                                     &output, &length) ==
           SLOPH_STATUS_INVALID_ARGUMENT);
    assert(sloph_context_diagnostic(context,
           sloph_context_diagnostic_count(context) - 1u, &diagnostic) ==
           SLOPH_STATUS_OK);
    assert(strcmp(diagnostic.code, "backend.c11.function_entry") == 0);
    sloph_core_free(unit); sloph_context_destroy(context);
}

static void test_v0_higher_order_parameter_matches_oracle(void) {
    static const unsigned char source[] =
        "(core 0 (types) (defs "
        "(def example::apply (fn (fn Int Int) Int) "
        "(lam (bind f (fn Int Int)) (app (local f) (int 1)))) "
        "(def example::main Int (int 0))))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    SlophDiagnosticView diagnostic;
    const char *binder;
    char *output = NULL;
    size_t length = 0u;
    assert(sloph_core_parse(context, source, sizeof(source) - 1u, &unit) ==
           SLOPH_STATUS_OK);
    assert(sloph_heartwood_to_timber(context, unit, "example::main",
                                     &output, &length) ==
           SLOPH_STATUS_INVALID_ARGUMENT);
    assert(sloph_context_diagnostic(context,
           sloph_context_diagnostic_count(context) - 1u, &diagnostic) ==
           SLOPH_STATUS_OK);
    assert(strcmp(diagnostic.code, "backend.c11.higher_order_type") == 0);
    assert(strcmp(diagnostic.phase, "backend") == 0);
    assert(strcmp(diagnostic.message,
        "the C11 first-order profile rejects function-typed function parameter") == 0);
    assert(strcmp(diagnostic.details_json,
                  "{\"role\":\"function parameter\"}") == 0);
    binder = strstr((const char *)source, "(bind f (fn Int Int))");
    assert(binder != NULL);
    assert(diagnostic.span.start == (size_t)(binder - (const char *)source));
    assert(diagnostic.span.end == diagnostic.span.start +
           strlen("(bind f (fn Int Int))"));
    sloph_core_free(unit); sloph_context_destroy(context);
}

static void test_v0_higher_order_let_matches_oracle(void) {
    static const unsigned char source[] =
        "(core 0 (types) (defs "
        "(def example::identity (fn Int Int) "
        "(lam (bind x Int) (local x))) "
        "(def example::main Int (let (bind f (fn Int Int)) "
        "(global example::identity) (int 0)))))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    SlophDiagnosticView diagnostic;
    const char *binder;
    char *output = NULL;
    size_t length = 0u;
    assert(sloph_core_parse(context, source, sizeof(source) - 1u, &unit) ==
           SLOPH_STATUS_OK);
    assert(sloph_heartwood_to_timber(context, unit, "example::main",
                                     &output, &length) ==
           SLOPH_STATUS_INVALID_ARGUMENT);
    assert(sloph_context_diagnostic(context,
           sloph_context_diagnostic_count(context) - 1u, &diagnostic) ==
           SLOPH_STATUS_OK);
    assert(strcmp(diagnostic.code, "backend.c11.higher_order_type") == 0);
    assert(strcmp(diagnostic.message,
        "the C11 first-order profile rejects function-typed let binding") == 0);
    assert(strcmp(diagnostic.details_json, "{\"role\":\"let binding\"}") == 0);
    binder = strstr((const char *)source, "(bind f (fn Int Int))");
    assert(binder != NULL);
    assert(diagnostic.span.start == (size_t)(binder - (const char *)source));
    assert(diagnostic.span.end == diagnostic.span.start +
           strlen("(bind f (fn Int Int))"));
    sloph_core_free(unit); sloph_context_destroy(context);
}

static void test_malformed_foreign_adapter_cannot_crash_emitter(void) {
    static const unsigned char source[] =
        "(core 1 (types) (defs (def example::main Int (prim evil::write))) "
        "(foreign (binding evil::write puts borrowed_bytes_write "
        "(params) (result Int) (c-params) (c-result int) "
        "(provider evil) (header stdio.h) (requires) (effects) (facts) "
        "(provenance evil))))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    SlophDiagnosticView diagnostic;
    SlophForeignRequirementView requirement;
    char *output = NULL;
    size_t length = 0u;
    assert(sloph_core_parse(context, source, sizeof(source) - 1u, &unit) ==
           SLOPH_STATUS_OK);
    assert(sloph_heartwood_foreign_requirement_count(unit) == 1u);
    assert(sloph_heartwood_foreign_requirement(unit, 0u, &requirement) ==
           SLOPH_STATUS_OK);
    assert(strcmp(requirement.identity, "evil::write") == 0);
    assert(strcmp(requirement.provider, "evil") == 0);
    assert(strcmp(requirement.symbol, "puts") == 0);
    assert(strcmp(requirement.header, "stdio.h") == 0);
    assert(sloph_heartwood_to_timber(context, unit, "example::main",
                                     &output, &length) ==
           SLOPH_STATUS_INVALID_ARGUMENT);
    assert(output == NULL && length == 0u);
    assert(sloph_context_diagnostic(context,
           sloph_context_diagnostic_count(context) - 1u, &diagnostic) ==
           SLOPH_STATUS_OK);
    assert(strcmp(diagnostic.code, "backend.c11.foreign_adapter") == 0);
    sloph_core_free(unit); sloph_context_destroy(context);
}

int main(void) {
    test_deterministic_heartwood_to_timber();
    test_unknown_entry_is_structured();
    test_function_entry_rejection_is_structured();
    test_v0_higher_order_parameter_matches_oracle();
    test_v0_higher_order_let_matches_oracle();
    test_malformed_foreign_adapter_cannot_crash_emitter();
    return 0;
}
