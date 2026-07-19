#include "sloph/sloph.h"
#include "../src/internal.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

typedef struct FailingAllocator {
    size_t calls;
    size_t successful_calls;
} FailingAllocator;

typedef struct TrackingAllocator {
    size_t calls;
    size_t fail_after;
    size_t active_allocations;
    size_t active_bytes;
} TrackingAllocator;

static void *tracking_allocate(void *user_data, size_t size) {
    TrackingAllocator *state = user_data;
    void *pointer;
    if (state->calls++ >= state->fail_after) return NULL;
    pointer = malloc(size);
    if (pointer != NULL) {
        ++state->active_allocations;
        state->active_bytes += size;
    }
    return pointer;
}

static void *tracking_resize(void *user_data, void *pointer, size_t old_size,
                             size_t new_size) {
    TrackingAllocator *state = user_data;
    void *resized;
    if (state->calls++ >= state->fail_after) return NULL;
    resized = realloc(pointer, new_size);
    if (resized != NULL && pointer == NULL) {
        ++state->active_allocations;
        state->active_bytes += new_size;
    } else if (resized != NULL) {
        if (new_size >= old_size) state->active_bytes += new_size - old_size;
        else state->active_bytes -= old_size - new_size;
    }
    return resized;
}

static void tracking_deallocate(void *user_data, void *pointer, size_t size) {
    TrackingAllocator *state = user_data;
    if (pointer == NULL) return;
    assert(state->active_allocations != 0u);
    assert(state->active_bytes >= size);
    --state->active_allocations;
    state->active_bytes -= size;
    free(pointer);
}

static SlophContext *tracking_context(TrackingAllocator *state) {
    SlophContextConfig config = sloph_context_config_default();
    SlophContext *context = NULL;
    config.allocator.user_data = state;
    config.allocator.allocate = tracking_allocate;
    config.allocator.resize = tracking_resize;
    config.allocator.deallocate = tracking_deallocate;
    assert(sloph_context_create(&config, &context) == SLOPH_STATUS_OK);
    return context;
}

static void *failing_allocate(void *user_data, size_t size) {
    FailingAllocator *state = user_data;
    if (state->calls++ >= state->successful_calls) return NULL;
    return malloc(size);
}

static void *failing_resize(void *user_data, void *pointer, size_t old_size,
                            size_t new_size) {
    FailingAllocator *state = user_data;
    (void)old_size;
    if (state->calls++ >= state->successful_calls) return NULL;
    return realloc(pointer, new_size);
}

static void failing_deallocate(void *user_data, void *pointer, size_t size) {
    (void)user_data;
    (void)size;
    free(pointer);
}

static void test_limits(void) {
    SlophLimits limits = sloph_limits_default();
    assert(limits.input_bytes == 1048576u);
    assert(limits.integer_bits == 16384u);
    assert(sloph_limits_validate(&limits) == SLOPH_STATUS_OK);
    limits.fuel = 0u;
    assert(sloph_limits_validate(&limits) == SLOPH_STATUS_INVALID_ARGUMENT);
}

static void test_diagnostics_and_storage(void) {
    SlophContextConfig config = sloph_context_config_default();
    SlophContext *context = NULL;
    SlophDiagnosticView view;
    SlophArena arena;
    SlophBuffer buffer;
    char *copy = NULL;
    config.max_diagnostics = 1u;
    assert(sloph_context_create(&config, &context) == SLOPH_STATUS_OK);
    assert(sloph_context_add_diagnostic(
               context, "test.failure", "test", "expected failure",
               (SlophSpan){2u, 7u}) == SLOPH_STATUS_OK);
    assert(sloph_context_add_diagnostic(context, "test.extra", "test", "extra",
                                        SLOPH_UNKNOWN_SPAN) ==
           SLOPH_STATUS_LIMIT_EXCEEDED);
    assert(sloph_context_diagnostic_count(context) == 1u);
    assert(sloph_context_diagnostic(context, 0u, &view) == SLOPH_STATUS_OK);
    assert(strcmp(view.code, "test.failure") == 0);
    assert(view.span.start == 2u && view.span.end == 7u);

    sloph_buffer_init(&buffer, context, 3u);
    assert(sloph_buffer_append(&buffer, "abc", 3u) == SLOPH_STATUS_OK);
    assert(sloph_buffer_append_byte(&buffer, '!') ==
           SLOPH_STATUS_LIMIT_EXCEEDED);
    sloph_buffer_destroy(&buffer);

    sloph_arena_init(&arena, context, 64u, 32u);
    assert(sloph_arena_copy_string(&arena, "hello", 5u, &copy) ==
           SLOPH_STATUS_OK);
    assert(strcmp(copy, "hello") == 0);
    sloph_arena_destroy(&arena);
    sloph_context_destroy(context);
}

static void test_allocation_failure(void) {
    FailingAllocator state = {0u, 1u};
    SlophContextConfig config = sloph_context_config_default();
    SlophContext *context = NULL;
    config.allocator.user_data = &state;
    config.allocator.allocate = failing_allocate;
    config.allocator.resize = failing_resize;
    config.allocator.deallocate = failing_deallocate;
    assert(sloph_context_create(&config, &context) == SLOPH_STATUS_OK);
    assert(sloph_context_add_diagnostic(context, "test.oom", "test", "oom",
                                        SLOPH_UNKNOWN_SPAN) ==
           SLOPH_STATUS_OUT_OF_MEMORY);
    assert(sloph_context_diagnostic_count(context) == 0u);
    sloph_context_destroy(context);
}

static void test_context_owned_outputs_and_yyjson(void) {
    static const unsigned char core[] =
        "(core 0 (types) (defs (def example::main Int (int 42))))";
    static const unsigned char source[] =
        "module example::main; public value main:Int { 42 }";
    TrackingAllocator state = {0u, (size_t)-1, 0u, 0u};
    SlophContext *context = tracking_context(&state);
    SlophCoreUnit *unit = NULL;
    SlophSyntaxModule *module = NULL;
    SlophSyntaxText json = {0};
    char *printed = NULL;
    size_t printed_length = 0u;
    size_t before_json, after_json_calls;
    assert(sloph_core_parse(context, core, sizeof(core) - 1u, &unit) ==
           SLOPH_STATUS_OK);
    assert(sloph_core_print(context, unit, &printed, &printed_length) ==
           SLOPH_STATUS_OK);
    assert(state.active_allocations >= 2u);
    sloph_context_deallocate(context, printed, printed_length + 1u);
    assert(sloph_syntax_parse(context, source, sizeof(source) - 1u, 0u,
                              &module) == SLOPH_STATUS_OK);
    before_json = state.active_allocations;
    assert(sloph_syntax_to_json(context, module, &json) == SLOPH_STATUS_OK);
    after_json_calls = state.calls;
    assert(after_json_calls > 0u);
    sloph_syntax_text_free(context, &json);
    assert(state.active_allocations == before_json);
    state.fail_after = state.calls;
    assert(sloph_syntax_to_json(context, module, &json) ==
           SLOPH_STATUS_OUT_OF_MEMORY);
    assert(json.data == NULL && state.active_allocations == before_json);
    state.fail_after = (size_t)-1;
    sloph_core_free(unit);
    sloph_context_destroy(context);
    /* Syntax modules retain an allocator value, not a context pointer. */
    sloph_syntax_module_free(module);
    assert(state.active_allocations == 0u);
    assert(state.active_bytes == 0u);
}

static void test_posix_host(void) {
    SlophContext *context = NULL;
    SlophHost host = sloph_posix_host();
    SlophOwnedBytes read_back;
    SlophTargetInfo target;
    uint64_t first;
    uint64_t second;
    char path[] = "/tmp/sloph-foundation-XXXXXX";
    static const unsigned char contents[] = "foundation";
    int descriptor;
    descriptor = mkstemp(path);
    assert(descriptor >= 0);
    assert(close(descriptor) == 0);
    assert(sloph_context_create(NULL, &context) == SLOPH_STATUS_OK);
    assert(host.write_file_atomic(host.user_data, context, path,
                                  (SlophBytes){contents,
                                               sizeof(contents) - 1u},
                                  0600u) == SLOPH_STATUS_OK);
    assert(host.read_file(host.user_data, context, path, 100u, &read_back) ==
           SLOPH_STATUS_OK);
    assert(read_back.length == sizeof(contents) - 1u);
    assert(memcmp(read_back.data, contents, read_back.length) == 0);
    host.release_bytes(host.user_data, context, &read_back);
    assert(host.read_file(host.user_data, context, path, 3u, &read_back) ==
           SLOPH_STATUS_LIMIT_EXCEEDED);
    assert(host.target_info(host.user_data, &target) == SLOPH_STATUS_OK);
    assert(target.operating_system != NULL && target.architecture != NULL);
    assert(host.monotonic_nanoseconds(host.user_data, &first) ==
           SLOPH_STATUS_OK);
    assert(host.monotonic_nanoseconds(host.user_data, &second) ==
           SLOPH_STATUS_OK);
    assert(second >= first);
    assert(unlink(path) == 0);
    sloph_context_destroy(context);
}

int main(void) {
    test_limits();
    test_diagnostics_and_storage();
    test_allocation_failure();
    test_context_owned_outputs_and_yyjson();
    test_posix_host();
    return 0;
}
