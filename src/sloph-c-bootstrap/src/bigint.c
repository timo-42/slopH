#include "bigint.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void normalize(SlophBigInt *value) {
    while (value->length > 0u && value->limbs[value->length - 1u] == 0u) {
        --value->length;
    }
    if (value->length == 0u) {
        value->sign = 0;
    }
}

static bool allocate(SlophBigInt *value, size_t count) {
    uint32_t *limbs = NULL;
    if (count > SIZE_MAX / sizeof(*limbs)) {
        return false;
    }
    if (count != 0u) {
        limbs = (uint32_t *)calloc(count, sizeof(*limbs));
        if (limbs == NULL) {
            return false;
        }
    }
    sloph_bigint_destroy(value);
    value->limbs = limbs;
    value->length = count;
    value->sign = count == 0u ? 0 : 1;
    return true;
}

void sloph_bigint_init(SlophBigInt *value) {
    value->sign = 0;
    value->length = 0u;
    value->limbs = NULL;
}

void sloph_bigint_destroy(SlophBigInt *value) {
    free(value->limbs);
    sloph_bigint_init(value);
}

bool sloph_bigint_copy(SlophBigInt *result, const SlophBigInt *value) {
    SlophBigInt copy;
    sloph_bigint_init(&copy);
    if (!allocate(&copy, value->length)) {
        return false;
    }
    if (value->length != 0u) {
        memcpy(copy.limbs, value->limbs, value->length * sizeof(*copy.limbs));
    }
    copy.sign = value->sign;
    sloph_bigint_destroy(result);
    *result = copy;
    return true;
}

size_t sloph_bigint_bits(const SlophBigInt *value) {
    uint32_t top;
    size_t bits;
    if (value->length == 0u) {
        return 0u;
    }
    top = value->limbs[value->length - 1u];
    bits = (value->length - 1u) * 32u;
    while (top != 0u) {
        ++bits;
        top >>= 1u;
    }
    return bits;
}

size_t sloph_bigint_u64_limbs(const SlophBigInt *value) {
    size_t bits = sloph_bigint_bits(value);
    return bits == 0u ? 1u : (bits + 63u) / 64u;
}

bool sloph_bigint_parse_decimal(SlophBigInt *result, const char *text,
                               size_t length, size_t maximum_bits) {
    SlophBigInt parsed;
    size_t offset = 0u;
    size_t capacity = maximum_bits / 32u + (maximum_bits % 32u != 0u);
    int sign = 1;
    sloph_bigint_init(&parsed);
    if (length != 0u && text[0] == '-') {
        sign = -1;
        offset = 1u;
    }
    if (offset == length || !allocate(&parsed, capacity)) {
        return false;
    }
    parsed.length = 0u;
    for (; offset < length; ++offset) {
        uint64_t carry;
        size_t index;
        unsigned char byte = (unsigned char)text[offset];
        if (byte < (unsigned char)'0' || byte > (unsigned char)'9') {
            sloph_bigint_destroy(&parsed);
            return false;
        }
        carry = (uint64_t)(byte - (unsigned char)'0');
        for (index = 0u; index < parsed.length; ++index) {
            uint64_t current = (uint64_t)parsed.limbs[index] * 10u + carry;
            parsed.limbs[index] = (uint32_t)current;
            carry = current >> 32u;
        }
        if (carry != 0u) {
            if (parsed.length == capacity) {
                sloph_bigint_destroy(&parsed);
                return false;
            }
            parsed.limbs[parsed.length++] = (uint32_t)carry;
        }
        if (sloph_bigint_bits(&parsed) > maximum_bits) {
            sloph_bigint_destroy(&parsed);
            return false;
        }
    }
    normalize(&parsed);
    if (parsed.length != 0u) {
        parsed.sign = sign;
    }
    sloph_bigint_destroy(result);
    *result = parsed;
    return true;
}

static int magnitude_compare(const SlophBigInt *left,
                             const SlophBigInt *right) {
    size_t index;
    if (left->length != right->length) {
        return left->length < right->length ? -1 : 1;
    }
    index = left->length;
    while (index != 0u) {
        --index;
        if (left->limbs[index] != right->limbs[index]) {
            return left->limbs[index] < right->limbs[index] ? -1 : 1;
        }
    }
    return 0;
}

int sloph_bigint_compare(const SlophBigInt *left, const SlophBigInt *right) {
    int magnitude;
    if (left->sign != right->sign) {
        return left->sign < right->sign ? -1 : 1;
    }
    if (left->sign == 0) {
        return 0;
    }
    magnitude = magnitude_compare(left, right);
    return left->sign < 0 ? -magnitude : magnitude;
}

static bool magnitude_add(SlophBigInt *result, const SlophBigInt *left,
                          const SlophBigInt *right) {
    size_t count = left->length > right->length ? left->length : right->length;
    size_t index;
    uint64_t carry = 0u;
    if (!allocate(result, count + 1u)) {
        return false;
    }
    for (index = 0u; index < count; ++index) {
        uint64_t a = index < left->length ? left->limbs[index] : 0u;
        uint64_t b = index < right->length ? right->limbs[index] : 0u;
        uint64_t sum = a + b + carry;
        result->limbs[index] = (uint32_t)sum;
        carry = sum >> 32u;
    }
    result->limbs[count] = (uint32_t)carry;
    result->length = count + (carry != 0u ? 1u : 0u);
    return true;
}

static bool magnitude_subtract(SlophBigInt *result, const SlophBigInt *large,
                               const SlophBigInt *small) {
    size_t index;
    uint64_t borrow = 0u;
    if (!allocate(result, large->length)) {
        return false;
    }
    for (index = 0u; index < large->length; ++index) {
        uint64_t a = large->limbs[index];
        uint64_t b = (index < small->length ? small->limbs[index] : 0u) + borrow;
        result->limbs[index] = (uint32_t)(a - b);
        borrow = a < b ? 1u : 0u;
    }
    normalize(result);
    return true;
}

static bool add_signed(SlophBigInt *result, const SlophBigInt *left,
                       const SlophBigInt *right, int right_sign,
                       size_t maximum_bits) {
    SlophBigInt temporary;
    int comparison;
    bool success;
    sloph_bigint_init(&temporary);
    if (left->sign == 0) {
        success = sloph_bigint_copy(&temporary, right);
        temporary.sign = right->length == 0u ? 0 : right_sign;
    } else if (right_sign == 0) {
        success = sloph_bigint_copy(&temporary, left);
    } else if (left->sign == right_sign) {
        success = magnitude_add(&temporary, left, right);
        temporary.sign = left->sign;
    } else {
        comparison = magnitude_compare(left, right);
        if (comparison == 0) {
            success = true;
        } else if (comparison > 0) {
            success = magnitude_subtract(&temporary, left, right);
            temporary.sign = left->sign;
        } else {
            success = magnitude_subtract(&temporary, right, left);
            temporary.sign = right_sign;
        }
    }
    if (!success || sloph_bigint_bits(&temporary) > maximum_bits) {
        sloph_bigint_destroy(&temporary);
        return false;
    }
    sloph_bigint_destroy(result);
    *result = temporary;
    return true;
}

bool sloph_bigint_add(SlophBigInt *result, const SlophBigInt *left,
                     const SlophBigInt *right, size_t maximum_bits) {
    return add_signed(result, left, right, right->sign, maximum_bits);
}

bool sloph_bigint_subtract(SlophBigInt *result, const SlophBigInt *left,
                          const SlophBigInt *right, size_t maximum_bits) {
    return add_signed(result, left, right, -right->sign, maximum_bits);
}

bool sloph_bigint_multiply(SlophBigInt *result, const SlophBigInt *left,
                          const SlophBigInt *right, size_t maximum_bits) {
    SlophBigInt product;
    size_t index;
    sloph_bigint_init(&product);
    if (left->sign == 0 || right->sign == 0) {
        sloph_bigint_destroy(result);
        *result = product;
        return true;
    }
    if (left->length > SIZE_MAX - right->length ||
        !allocate(&product, left->length + right->length)) {
        return false;
    }
    for (index = 0u; index < left->length; ++index) {
        size_t inner;
        uint64_t carry = 0u;
        for (inner = 0u; inner < right->length; ++inner) {
            size_t position = index + inner;
            uint64_t current = (uint64_t)left->limbs[index] * right->limbs[inner]
                             + product.limbs[position] + carry;
            product.limbs[position] = (uint32_t)current;
            carry = current >> 32u;
        }
        {
            size_t position = index + right->length;
            while (carry != 0u) {
                uint64_t current = (uint64_t)product.limbs[position] + carry;
                product.limbs[position] = (uint32_t)current;
                carry = current >> 32u;
                ++position;
            }
        }
    }
    product.sign = left->sign * right->sign;
    normalize(&product);
    if (sloph_bigint_bits(&product) > maximum_bits) {
        sloph_bigint_destroy(&product);
        return false;
    }
    sloph_bigint_destroy(result);
    *result = product;
    return true;
}

bool sloph_bigint_decimal(const SlophBigInt *value, char **text,
                          size_t *length) {
    uint32_t *work = NULL;
    uint32_t *chunks = NULL;
    char *output = NULL;
    size_t work_length = value->length;
    size_t count = 0u;
    size_t capacity;
    size_t offset = 0u;
    if (value->length == 0u) {
        output = (char *)malloc(2u);
        if (output == NULL) return false;
        output[0] = '0'; output[1] = '\0';
        *text = output; *length = 1u;
        return true;
    }
    work = (uint32_t *)malloc(value->length * sizeof(*work));
    chunks = (uint32_t *)malloc((value->length * 10u / 9u + 1u) * sizeof(*chunks));
    if (work == NULL || chunks == NULL) goto failure;
    memcpy(work, value->limbs, value->length * sizeof(*work));
    while (work_length != 0u) {
        uint64_t remainder = 0u;
        size_t index = work_length;
        while (index != 0u) {
            uint64_t current;
            --index;
            current = (remainder << 32u) | work[index];
            work[index] = (uint32_t)(current / 1000000000u);
            remainder = current % 1000000000u;
        }
        chunks[count++] = (uint32_t)remainder;
        while (work_length != 0u && work[work_length - 1u] == 0u) --work_length;
    }
    capacity = count * 9u + 2u;
    output = (char *)malloc(capacity);
    if (output == NULL) goto failure;
    if (value->sign < 0) output[offset++] = '-';
    {
        int written = snprintf(output + offset, capacity - offset, "%u", chunks[count - 1u]);
        if (written < 0) goto failure;
        offset += (size_t)written;
    }
    while (--count != 0u) {
        int written = snprintf(output + offset, capacity - offset, "%09u", chunks[count - 1u]);
        if (written != 9) goto failure;
        offset += 9u;
    }
    output[offset] = '\0';
    free(work); free(chunks);
    *text = output; *length = offset;
    return true;
failure:
    free(work); free(chunks); free(output);
    return false;
}
