#ifndef SLOPH_CONTEXT_H
#define SLOPH_CONTEXT_H

#include "sloph/diagnostic.h"
#include "sloph/limits.h"

typedef struct SlophContext SlophContext;

typedef struct SlophContextConfig {
    SlophAllocator allocator;
    SlophLimits limits;
    size_t max_diagnostics;
} SlophContextConfig;

SlophContextConfig sloph_context_config_default(void);
SlophStatus sloph_context_create(const SlophContextConfig *config,
                                 SlophContext **out_context);
void sloph_context_destroy(SlophContext *context);
void sloph_context_clear_diagnostics(SlophContext *context);
const SlophLimits *sloph_context_limits(const SlophContext *context);
const SlophAllocator *sloph_context_allocator(const SlophContext *context);

size_t sloph_context_diagnostic_count(const SlophContext *context);
SlophStatus sloph_context_diagnostic(const SlophContext *context, size_t index,
                                     SlophDiagnosticView *out_diagnostic);
SlophStatus sloph_context_add_diagnostic(
    SlophContext *context, const char *code, const char *phase,
    const char *message, SlophSpan span);
SlophStatus sloph_context_add_diagnostic_full(
    SlophContext *context, const char *code, const char *phase,
    const char *message, const char *details_json, SlophSpan span,
    SlophSeverity severity);

#endif
