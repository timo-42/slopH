#include "syntax_internal.h"

#include <string.h>

SlophStatus sloph_syntax_new_module(SlophContext *context, unsigned version,
                                    SlophSyntaxModule **out_module) {
    const SlophAllocator *allocator;
    SlophSyntaxModule *module;
    if (context == NULL || out_module == NULL || version > 1u)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    *out_module = NULL;
    allocator = sloph_context_allocator(context);
    module = allocator->allocate(allocator->user_data, sizeof(*module));
    if (module == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    memset(module, 0, sizeof(*module));
    module->allocator = *allocator;
    module->version = version;
    sloph_arena_init(&module->arena, context,
                     sloph_context_limits(context)->project_bytes, 4096u);
    *out_module = module;
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_syntax_alloc(SlophSyntaxModule *module, size_t size,
                               size_t alignment, void **out) {
    SlophStatus status;
    if (module == NULL || out == NULL) return SLOPH_STATUS_INVALID_ARGUMENT;
    status = sloph_arena_allocate(&module->arena, size, alignment, out);
    if (status == SLOPH_STATUS_OK) memset(*out, 0, size);
    return status;
}

SlophStatus sloph_syntax_string(SlophSyntaxModule *module, const char *source,
                                size_t length, char **out) {
    return sloph_arena_copy_string(&module->arena, source, length, out);
}

SlophStatus sloph_syntax_diagnostic(SlophContext *context, const char *code,
                                    const char *phase, const char *message,
                                    SlophSpan span, SlophStatus status) {
    SlophStatus added = sloph_context_add_diagnostic(context, code, phase,
                                                     message, span);
    return added == SLOPH_STATUS_OK ? status : added;
}

void sloph_syntax_module_free(SlophSyntaxModule *module) {
    SlophAllocator allocator;
    if (module == NULL) return;
    allocator = module->allocator;
    sloph_arena_destroy(&module->arena);
    allocator.deallocate(allocator.user_data, module, sizeof(*module));
}

void sloph_syntax_text_free(SlophContext *context, SlophSyntaxText *text) {
    const SlophAllocator *allocator;
    if (context == NULL || text == NULL || text->data == NULL) return;
    allocator = sloph_context_allocator(context);
    allocator->deallocate(allocator->user_data, text->data, text->length + 1u);
    text->data = NULL;
    text->length = 0u;
}
