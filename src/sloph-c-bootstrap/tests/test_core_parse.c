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
    free(output);
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
    free(output);
    sloph_core_free(unit);
    sloph_context_destroy(context);
}

int main(void) {
    canonical_order_and_negative_zero();
    malformed_list_has_exact_span();
    version_three_ownership_round_trip();
    return 0;
}
