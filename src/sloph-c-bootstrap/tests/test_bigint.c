#include "../src/bigint.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

static void expect(const SlophBigInt *value, const char *wanted) {
    char *actual = NULL;
    size_t length = 0u;
    assert(sloph_bigint_decimal(value, &actual, &length));
    assert(length == strlen(wanted));
    assert(strcmp(actual, wanted) == 0);
    free(actual);
}

int main(void) {
    SlophBigInt a, b, result;
    sloph_bigint_init(&a); sloph_bigint_init(&b); sloph_bigint_init(&result);
    assert(sloph_bigint_parse_decimal(&a, "12345678901234567890", 20u, 128u));
    assert(sloph_bigint_parse_decimal(&b, "-9", 2u, 128u));
    assert(sloph_bigint_multiply(&result, &a, &b, 128u));
    expect(&result, "-111111110111111111010");
    assert(sloph_bigint_add(&result, &a, &b, 128u));
    expect(&result, "12345678901234567881");
    assert(sloph_bigint_subtract(&result, &b, &a, 128u));
    expect(&result, "-12345678901234567899");
    assert(!sloph_bigint_parse_decimal(&result, "256", 3u, 8u));
    assert(sloph_bigint_compare(&a, &b) > 0);
    assert(sloph_bigint_u64_limbs(&a) == 1u);
    sloph_bigint_destroy(&a); sloph_bigint_destroy(&b); sloph_bigint_destroy(&result);
    return 0;
}
