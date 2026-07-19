#ifndef SLOPH_CORE_H
#define SLOPH_CORE_H

#include "sloph/base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlophContext SlophContext;
typedef struct SlophCoreUnit SlophCoreUnit;

/* Parse one ASCII Core s-expression. The returned unit is owned by the
 * caller. On failure a structured diagnostic is appended to context. */
SlophStatus sloph_core_parse(SlophContext *context, const unsigned char *input,
                             size_t input_length, SlophCoreUnit **out_unit);

/* Validate, canonicalize and print unit. The NUL-terminated result uses the
 * context allocator and must be released with
 * sloph_context_deallocate(context, text, out_length + 1). */
SlophStatus sloph_core_print(SlophContext *context, SlophCoreUnit *unit,
                             char **out_text, size_t *out_length);
SlophStatus sloph_core_validate(SlophContext *context, SlophCoreUnit *unit);
/* The evaluation text follows the same context-allocator ownership contract
 * as sloph_core_print. */
SlophStatus sloph_core_evaluate(SlophContext *context, SlophCoreUnit *unit,
                                const char *symbol, char **out_text,
                                size_t *out_length);

void sloph_core_free(SlophCoreUnit *unit);

#ifdef __cplusplus
}
#endif

#endif
