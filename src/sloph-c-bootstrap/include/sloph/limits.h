#ifndef SLOPH_LIMITS_H
#define SLOPH_LIMITS_H

#include "sloph/base.h"

typedef struct SlophLimits {
    size_t input_bytes;
    size_t tokens;
    size_t token_bytes;
    size_t syntax_depth;
    size_t ast_nodes;
    size_t literal_digits;
    size_t fuel;
    size_t integer_bits;
    size_t evaluation_depth;
    size_t value_nodes;
    size_t output_bytes;
    size_t transform_depth;
    size_t transform_expansions;
    size_t project_files;
    size_t project_bytes;
} SlophLimits;

SlophLimits sloph_limits_default(void);
SlophStatus sloph_limits_validate(const SlophLimits *limits);

#endif
