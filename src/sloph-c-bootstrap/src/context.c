#include "sloph/context.h"

#include <stdlib.h>
#include <string.h>

typedef struct SlophOwnedDiagnostic {
    char *code;
    char *phase;
    char *message;
    char *details_json;
    SlophSpan span;
    SlophSeverity severity;
    size_t owned_bytes;
} SlophOwnedDiagnostic;

struct SlophContext {
    SlophAllocator allocator;
    SlophLimits limits;
    SlophOwnedDiagnostic *diagnostics;
    size_t diagnostic_count;
    size_t diagnostic_capacity;
    size_t diagnostic_bytes;
    size_t max_diagnostics;
};

static void *system_allocate(void *user_data, size_t size) {
    (void)user_data;
    return malloc(size);
}

static void *system_resize(void *user_data, void *pointer, size_t old_size,
                           size_t new_size) {
    (void)user_data;
    (void)old_size;
    return realloc(pointer, new_size);
}

static void system_deallocate(void *user_data, void *pointer, size_t size) {
    (void)user_data;
    (void)size;
    free(pointer);
}

SlophAllocator sloph_system_allocator(void) {
    SlophAllocator allocator;
    allocator.user_data = NULL;
    allocator.allocate = system_allocate;
    allocator.resize = system_resize;
    allocator.deallocate = system_deallocate;
    return allocator;
}

const char *sloph_status_name(SlophStatus status) {
    switch (status) {
        case SLOPH_STATUS_OK: return "ok";
        case SLOPH_STATUS_INVALID_ARGUMENT: return "invalid_argument";
        case SLOPH_STATUS_OUT_OF_MEMORY: return "out_of_memory";
        case SLOPH_STATUS_LIMIT_EXCEEDED: return "limit_exceeded";
        case SLOPH_STATUS_IO_ERROR: return "io_error";
        case SLOPH_STATUS_PROCESS_ERROR: return "process_error";
        case SLOPH_STATUS_INTERNAL_ERROR: return "internal_error";
    }
    return "unknown";
}

const char *sloph_severity_name(SlophSeverity severity) {
    switch (severity) {
        case SLOPH_SEVERITY_ERROR: return "error";
        case SLOPH_SEVERITY_WARNING: return "warning";
        case SLOPH_SEVERITY_NOTE: return "note";
    }
    return "unknown";
}

SlophLimits sloph_limits_default(void) {
    SlophLimits limits;
    limits.input_bytes = 1048576u;
    limits.tokens = 100000u;
    limits.token_bytes = 4096u;
    limits.syntax_depth = 256u;
    limits.ast_nodes = 100000u;
    limits.literal_digits = 4096u;
    limits.fuel = 1000000u;
    limits.integer_bits = 16384u;
    limits.evaluation_depth = 4096u;
    limits.value_nodes = 100000u;
    limits.output_bytes = 1048576u;
    limits.transform_depth = 64u;
    limits.transform_expansions = 10000u;
    limits.project_files = 10000u;
    limits.project_bytes = 268435456u;
    return limits;
}

SlophStatus sloph_limits_validate(const SlophLimits *limits) {
    if (limits == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
#define SLOPH_REQUIRE_POSITIVE(field) \
    do { if (limits->field == 0u) return SLOPH_STATUS_INVALID_ARGUMENT; } while (0)
    SLOPH_REQUIRE_POSITIVE(input_bytes);
    SLOPH_REQUIRE_POSITIVE(tokens);
    SLOPH_REQUIRE_POSITIVE(token_bytes);
    SLOPH_REQUIRE_POSITIVE(syntax_depth);
    SLOPH_REQUIRE_POSITIVE(ast_nodes);
    SLOPH_REQUIRE_POSITIVE(literal_digits);
    SLOPH_REQUIRE_POSITIVE(fuel);
    SLOPH_REQUIRE_POSITIVE(integer_bits);
    SLOPH_REQUIRE_POSITIVE(evaluation_depth);
    SLOPH_REQUIRE_POSITIVE(value_nodes);
    SLOPH_REQUIRE_POSITIVE(output_bytes);
    SLOPH_REQUIRE_POSITIVE(transform_depth);
    SLOPH_REQUIRE_POSITIVE(transform_expansions);
    SLOPH_REQUIRE_POSITIVE(project_files);
    SLOPH_REQUIRE_POSITIVE(project_bytes);
#undef SLOPH_REQUIRE_POSITIVE
    return SLOPH_STATUS_OK;
}

SlophContextConfig sloph_context_config_default(void) {
    SlophContextConfig config;
    config.allocator = sloph_system_allocator();
    config.limits = sloph_limits_default();
    config.max_diagnostics = 100u;
    return config;
}

static int allocator_is_empty(const SlophAllocator *allocator) {
    return allocator->allocate == NULL && allocator->resize == NULL &&
           allocator->deallocate == NULL;
}

static int allocator_is_complete(const SlophAllocator *allocator) {
    return allocator->allocate != NULL && allocator->resize != NULL &&
           allocator->deallocate != NULL;
}

SlophStatus sloph_context_create(const SlophContextConfig *requested,
                                 SlophContext **out_context) {
    SlophContextConfig config;
    SlophContext *context;
    if (out_context == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
    *out_context = NULL;
    config = requested != NULL ? *requested : sloph_context_config_default();
    if (allocator_is_empty(&config.allocator)) {
        config.allocator = sloph_system_allocator();
    }
    if (!allocator_is_complete(&config.allocator) ||
        sloph_limits_validate(&config.limits) != SLOPH_STATUS_OK ||
        config.max_diagnostics == 0u) {
        return SLOPH_STATUS_INVALID_ARGUMENT;
    }
    context = config.allocator.allocate(config.allocator.user_data,
                                        sizeof(*context));
    if (context == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    context->allocator = config.allocator;
    context->limits = config.limits;
    context->diagnostics = NULL;
    context->diagnostic_count = 0u;
    context->diagnostic_capacity = 0u;
    context->diagnostic_bytes = 0u;
    context->max_diagnostics = config.max_diagnostics;
    *out_context = context;
    return SLOPH_STATUS_OK;
}

static void destroy_owned_diagnostic(SlophContext *context,
                                     SlophOwnedDiagnostic *diagnostic) {
    SlophAllocator *allocator = &context->allocator;
    size_t code_size = strlen(diagnostic->code) + 1u;
    size_t phase_size = strlen(diagnostic->phase) + 1u;
    size_t message_size = strlen(diagnostic->message) + 1u;
    size_t details_size = strlen(diagnostic->details_json) + 1u;
    allocator->deallocate(allocator->user_data, diagnostic->code, code_size);
    allocator->deallocate(allocator->user_data, diagnostic->phase, phase_size);
    allocator->deallocate(allocator->user_data, diagnostic->message,
                          message_size);
    allocator->deallocate(allocator->user_data, diagnostic->details_json,
                          details_size);
}

void sloph_context_clear_diagnostics(SlophContext *context) {
    size_t index;
    if (context == NULL) return;
    for (index = 0u; index < context->diagnostic_count; ++index) {
        destroy_owned_diagnostic(context, &context->diagnostics[index]);
    }
    context->diagnostic_count = 0u;
    context->diagnostic_bytes = 0u;
}

void sloph_context_destroy(SlophContext *context) {
    SlophAllocator allocator;
    size_t diagnostics_size;
    if (context == NULL) return;
    allocator = context->allocator;
    sloph_context_clear_diagnostics(context);
    diagnostics_size = context->diagnostic_capacity * sizeof(*context->diagnostics);
    if (context->diagnostics != NULL) {
        allocator.deallocate(allocator.user_data, context->diagnostics,
                             diagnostics_size);
    }
    allocator.deallocate(allocator.user_data, context, sizeof(*context));
}

const SlophLimits *sloph_context_limits(const SlophContext *context) {
    return context != NULL ? &context->limits : NULL;
}

const SlophAllocator *sloph_context_allocator(const SlophContext *context) {
    return context != NULL ? &context->allocator : NULL;
}

size_t sloph_context_diagnostic_count(const SlophContext *context) {
    return context != NULL ? context->diagnostic_count : 0u;
}

SlophStatus sloph_context_diagnostic(const SlophContext *context, size_t index,
                                     SlophDiagnosticView *out_diagnostic) {
    const SlophOwnedDiagnostic *owned;
    if (context == NULL || out_diagnostic == NULL ||
        index >= context->diagnostic_count) {
        return SLOPH_STATUS_INVALID_ARGUMENT;
    }
    owned = &context->diagnostics[index];
    out_diagnostic->code = owned->code;
    out_diagnostic->phase = owned->phase;
    out_diagnostic->message = owned->message;
    out_diagnostic->details_json = owned->details_json;
    out_diagnostic->span = owned->span;
    out_diagnostic->severity = owned->severity;
    return SLOPH_STATUS_OK;
}

static char *copy_text(SlophContext *context, const char *text, size_t size) {
    char *copy = context->allocator.allocate(context->allocator.user_data, size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

SlophStatus sloph_context_add_diagnostic_full(
    SlophContext *context, const char *code, const char *phase,
    const char *message, const char *details_json, SlophSpan span,
    SlophSeverity severity) {
    size_t sizes[4];
    size_t total;
    size_t new_capacity;
    size_t old_size;
    size_t new_size;
    SlophOwnedDiagnostic *grown;
    SlophOwnedDiagnostic diagnostic;
    const char *details = details_json != NULL ? details_json : "{}";
    if (context == NULL || code == NULL || phase == NULL || message == NULL ||
        span.end < span.start || severity < SLOPH_SEVERITY_ERROR ||
        severity > SLOPH_SEVERITY_NOTE) {
        return SLOPH_STATUS_INVALID_ARGUMENT;
    }
    if (context->diagnostic_count >= context->max_diagnostics) {
        return SLOPH_STATUS_LIMIT_EXCEEDED;
    }
    sizes[0] = strlen(code) + 1u;
    sizes[1] = strlen(phase) + 1u;
    sizes[2] = strlen(message) + 1u;
    sizes[3] = strlen(details) + 1u;
    total = sizes[0];
    if (SIZE_MAX - total < sizes[1]) return SLOPH_STATUS_LIMIT_EXCEEDED;
    total += sizes[1];
    if (SIZE_MAX - total < sizes[2]) return SLOPH_STATUS_LIMIT_EXCEEDED;
    total += sizes[2];
    if (SIZE_MAX - total < sizes[3]) return SLOPH_STATUS_LIMIT_EXCEEDED;
    total += sizes[3];
    if (total > context->limits.output_bytes - context->diagnostic_bytes) {
        return SLOPH_STATUS_LIMIT_EXCEEDED;
    }
    if (context->diagnostic_count == context->diagnostic_capacity) {
        if (context->diagnostic_capacity > SIZE_MAX / 2u)
            return SLOPH_STATUS_LIMIT_EXCEEDED;
        new_capacity = context->diagnostic_capacity == 0u
                           ? 4u : context->diagnostic_capacity * 2u;
        if (new_capacity > context->max_diagnostics)
            new_capacity = context->max_diagnostics;
        if (new_capacity > SIZE_MAX / sizeof(*context->diagnostics))
            return SLOPH_STATUS_LIMIT_EXCEEDED;
        old_size = context->diagnostic_capacity * sizeof(*context->diagnostics);
        new_size = new_capacity * sizeof(*context->diagnostics);
        grown = context->allocator.resize(context->allocator.user_data,
                                          context->diagnostics, old_size, new_size);
        if (grown == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
        context->diagnostics = grown;
        context->diagnostic_capacity = new_capacity;
    }
    diagnostic.code = copy_text(context, code, sizes[0]);
    diagnostic.phase = copy_text(context, phase, sizes[1]);
    diagnostic.message = copy_text(context, message, sizes[2]);
    diagnostic.details_json = copy_text(context, details, sizes[3]);
    if (diagnostic.code == NULL || diagnostic.phase == NULL ||
        diagnostic.message == NULL || diagnostic.details_json == NULL) {
        if (diagnostic.code != NULL) context->allocator.deallocate(
            context->allocator.user_data, diagnostic.code, sizes[0]);
        if (diagnostic.phase != NULL) context->allocator.deallocate(
            context->allocator.user_data, diagnostic.phase, sizes[1]);
        if (diagnostic.message != NULL) context->allocator.deallocate(
            context->allocator.user_data, diagnostic.message, sizes[2]);
        if (diagnostic.details_json != NULL) context->allocator.deallocate(
            context->allocator.user_data, diagnostic.details_json, sizes[3]);
        return SLOPH_STATUS_OUT_OF_MEMORY;
    }
    diagnostic.span = span;
    diagnostic.severity = severity;
    diagnostic.owned_bytes = total;
    context->diagnostics[context->diagnostic_count++] = diagnostic;
    context->diagnostic_bytes += total;
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_context_add_diagnostic(
    SlophContext *context, const char *code, const char *phase,
    const char *message, SlophSpan span) {
    return sloph_context_add_diagnostic_full(context, code, phase, message,
                                             "{}", span,
                                             SLOPH_SEVERITY_ERROR);
}
