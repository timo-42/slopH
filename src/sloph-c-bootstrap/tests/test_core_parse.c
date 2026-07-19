#include "sloph/context.h"
#include "sloph/core.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static SlophContext *new_context(void) {
    SlophContext *context = NULL;
    SlophContextConfig config = sloph_context_config_default();
    assert(sloph_context_create(&config, &context) == SLOPH_STATUS_OK);
    return context;
}

static void canonical_order_and_negative_zero(void) {
    static const char input[] =
        "(core 0 (types) (defs (def e::z Int (int -0)) "
        "(def e::a Int (int 1))))";
    static const char expected[] =
        "(core 0 (types) (defs (def e::a Int (int 1)) "
        "(def e::z Int (int 0))))\n";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    char *output = NULL;
    size_t output_length = 0;
    assert(sloph_core_parse(context, (const unsigned char *)input,
                            strlen(input), &unit) == SLOPH_STATUS_OK);
    assert(sloph_core_print(context, unit, &output, &output_length) ==
           SLOPH_STATUS_OK);
    assert(output_length == strlen(expected));
    assert(memcmp(output, expected, output_length) == 0);
    sloph_context_deallocate(context, output, output_length + 1u);
    sloph_core_free(unit);
    sloph_context_destroy(context);
}

static void malformed_list_has_exact_span(void) {
    static const char input[] = "(core 0 (types) (defs)";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    SlophDiagnosticView diagnostic;
    assert(sloph_core_parse(context, (const unsigned char *)input,
                            strlen(input), &unit) ==
           SLOPH_STATUS_INVALID_ARGUMENT);
    assert(unit == NULL);
    assert(sloph_context_diagnostic_count(context) == 1);
    assert(sloph_context_diagnostic(context, 0, &diagnostic) == SLOPH_STATUS_OK);
    assert(strcmp(diagnostic.code, "core.parse.unclosed_list") == 0);
    assert(diagnostic.span.start == 0);
    assert(diagnostic.span.end == strlen(input));
    sloph_context_destroy(context);
}

static void version_three_ownership_round_trip(void) {
    static const char input[] =
        "(core 3 (types (enum e::E (ownership owned) (params) "
        "(ctor e::E::C))) (defs))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    char *output = NULL;
    size_t length = 0;
    assert(sloph_core_parse(context, (const unsigned char *)input,
                            strlen(input), &unit) == SLOPH_STATUS_OK);
    assert(sloph_core_print(context, unit, &output, &length) == SLOPH_STATUS_OK);
    assert(length == strlen(input) + 1);
    assert(memcmp(output, input, strlen(input)) == 0);
    assert(output[length - 1] == '\n');
    sloph_context_deallocate(context, output, length + 1u);
    sloph_core_free(unit);
    sloph_context_destroy(context);
}

static void mutable_borrow_round_trip(void) {
    static const char input[] =
        "(core 3 (types (enum e::R (ownership owned) (params) (ctor e::R::R))) "
        "(defs (def e::inspect (fn borrow-mut (named e::R) Int) "
        "(lam (bind borrow-mut item (named e::R)) (int 0)))))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    char *output = NULL;
    size_t length = 0;
    assert(sloph_core_parse(context, (const unsigned char *)input,
                            strlen(input), &unit) == SLOPH_STATUS_OK);
    assert(sloph_core_print(context, unit, &output, &length) == SLOPH_STATUS_OK);
    assert(strstr(output, "(fn borrow-mut (named e::R) Int)") != NULL);
    assert(strstr(output, "(bind borrow-mut v0 (named e::R))") != NULL);
    free(output);
    sloph_core_free(unit);
    sloph_context_destroy(context);
}

static void overlapping_mutable_borrow_is_rejected(void) {
    static const char input[] =
        "(core 3 (types (enum e::R (ownership owned) (params) (ctor e::R::R))) "
        "(defs "
        "(def e::both (fn borrow (named e::R) (fn borrow-mut (named e::R) Int)) "
        "(lam (bind borrow a (named e::R)) (lam (bind borrow-mut b (named e::R)) (int 0)))) "
        "(def e::drop (fn (named e::R) Int) (lam (bind x (named e::R)) "
        "(case (local x) Int (alt e::R::R (int 0))))) "
        "(def e::bad (fn (named e::R) Int) (lam (bind x (named e::R)) "
        "(let (bind n Int) (app (app (global e::both) (local x)) (local x)) "
        "(app (global e::drop) (local x)))))))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    SlophDiagnosticView diagnostic;
    assert(sloph_core_parse(context, (const unsigned char *)input,
                            strlen(input), &unit) == SLOPH_STATUS_OK);
    assert(sloph_core_validate(context, unit) == SLOPH_STATUS_INVALID_ARGUMENT);
    assert(sloph_context_diagnostic_count(context) == 1u);
    assert(sloph_context_diagnostic(context, 0u, &diagnostic) == SLOPH_STATUS_OK);
    assert(strcmp(diagnostic.code, "core.validate.borrow_conflict") == 0);
    sloph_core_free(unit);
    sloph_context_destroy(context);
}

static void branch_ownership_must_match(void) {
    static const char input[] =
        "(core 3 (types "
        "(enum e::R (ownership owned) (params) (ctor e::R::R)) "
        "(enum e::B (ownership copy) (params) (ctor e::B::T) (ctor e::B::F))) "
        "(defs "
        "(def e::drop (fn (named e::R) Int) (lam (bind x (named e::R)) "
        "(case (local x) Int (alt e::R::R (int 0))))) "
        "(def e::bad (fn (named e::R) (fn (named e::B) Int)) "
        "(lam (bind x (named e::R)) (lam (bind choice (named e::B)) "
        "(case (local choice) Int "
        "(alt e::B::T (app (global e::drop) (local x))) "
        "(alt e::B::F (int 0))))))))";
    SlophContext *context = new_context();
    SlophCoreUnit *unit = NULL;
    SlophDiagnosticView diagnostic;
    assert(sloph_core_parse(context, (const unsigned char *)input,
                            strlen(input), &unit) == SLOPH_STATUS_OK);
    assert(sloph_core_validate(context, unit) == SLOPH_STATUS_INVALID_ARGUMENT);
    assert(sloph_context_diagnostic(context, 0u, &diagnostic) == SLOPH_STATUS_OK);
    assert(strcmp(diagnostic.code, "core.validate.branch_ownership") == 0);
    sloph_core_free(unit);
    sloph_context_destroy(context);
}

int main(void) {
    canonical_order_and_negative_zero();
    malformed_list_has_exact_span();
    version_three_ownership_round_trip();
    mutable_borrow_round_trip();
    overlapping_mutable_borrow_is_rejected();
    branch_ownership_must_match();
    return 0;
}
