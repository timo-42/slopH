#ifndef SLOPH_BIGINT_H
#define SLOPH_BIGINT_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

typedef struct SlophBigInt {
    int sign;
    size_t length;
    uint32_t *limbs;
} SlophBigInt;

void sloph_bigint_init(SlophBigInt *value);
void sloph_bigint_destroy(SlophBigInt *value);
bool sloph_bigint_copy(SlophBigInt *result, const SlophBigInt *value);
bool sloph_bigint_parse_decimal(SlophBigInt *result, const char *text,
                               size_t length, size_t maximum_bits);
bool sloph_bigint_add(SlophBigInt *result, const SlophBigInt *left,
                     const SlophBigInt *right, size_t maximum_bits);
bool sloph_bigint_subtract(SlophBigInt *result, const SlophBigInt *left,
                          const SlophBigInt *right, size_t maximum_bits);
bool sloph_bigint_multiply(SlophBigInt *result, const SlophBigInt *left,
                          const SlophBigInt *right, size_t maximum_bits);
int sloph_bigint_compare(const SlophBigInt *left, const SlophBigInt *right);
size_t sloph_bigint_bits(const SlophBigInt *value);
size_t sloph_bigint_u64_limbs(const SlophBigInt *value);
bool sloph_bigint_decimal(const SlophBigInt *value, char **text,
                          size_t *length);

#endif
