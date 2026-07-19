#include "project_internal.h"
#include "sloph/syntax.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

SlophStatus sloph_project_diag(SlophContext *context, SlophStatus status,
                               const char *code, const char *phase,
                               const char *message, const char *details) {
    SlophStatus added = sloph_context_add_diagnostic_full(
        context, code, phase, message, details, SLOPH_UNKNOWN_SPAN,
        SLOPH_SEVERITY_ERROR);
    return added == SLOPH_STATUS_OK ? status : added;
}

SlophStatus sloph_project_arena_string(SlophProject *project,
                                       const char *text, char **out) {
    return sloph_arena_copy_string(&project->arena, text, strlen(text), out);
}

SlophStatus sloph_project_join_path(SlophProject *project, const char *left,
                                    const char *right, char **out) {
    size_t left_length = strlen(left);
    size_t right_length = strlen(right);
    size_t length;
    char *joined;
    int slash = left_length != 0u && left[left_length - 1u] != '/';
    if (!sloph_size_add(left_length, right_length, &length) ||
        !sloph_size_add(length, slash ? 2u : 1u, &length)) {
        return SLOPH_STATUS_LIMIT_EXCEEDED;
    }
    if (sloph_arena_allocate(&project->arena, length, 1u,
                             (void **)&joined) != SLOPH_STATUS_OK) {
        return SLOPH_STATUS_OUT_OF_MEMORY;
    }
    memcpy(joined, left, left_length);
    if (slash) joined[left_length++] = '/';
    memcpy(joined + left_length, right, right_length + 1u);
    *out = joined;
    return SLOPH_STATUS_OK;
}

static int path_kind(const char *path, int directory) {
    struct stat info;
    if (stat(path, &info) != 0) return 0;
    return directory ? S_ISDIR(info.st_mode) : S_ISREG(info.st_mode);
}

int sloph_project_regular_file(const char *path) { return path_kind(path, 0); }
int sloph_project_directory(const char *path) { return path_kind(path, 1); }

int sloph_project_lower_segment(const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    if (!(cursor[0] == '_' || (cursor[0] >= 'a' && cursor[0] <= 'z')))
        return 0;
    for (++cursor; *cursor != '\0'; ++cursor) {
        if (!(*cursor == '_' || (*cursor >= 'a' && *cursor <= 'z') ||
              (*cursor >= 'A' && *cursor <= 'Z') ||
              (*cursor >= '0' && *cursor <= '9'))) return 0;
    }
    return 1;
}

int sloph_project_global(const char *text) {
    const char *segment = text;
    const char *cursor = text;
    int count = 0;
    char local[256];
    for (;;) {
        if ((cursor[0] == ':' && cursor[1] == ':') || cursor[0] == '\0') {
            size_t length = (size_t)(cursor - segment);
            if (length == 0u || length >= sizeof(local)) return 0;
            memcpy(local, segment, length); local[length] = '\0';
            if (!sloph_project_lower_segment(local)) return 0;
            ++count;
            if (cursor[0] == '\0') break;
            cursor += 2; segment = cursor; continue;
        }
        ++cursor;
    }
    return count >= 2;
}

SlophProjectOptions sloph_project_options_default(void) {
    SlophProjectOptions options;
    memset(&options, 0, sizeof(options));
    options.source_version = 1u;
    return options;
}

static void free_vector(SlophProject *project, void *pointer, size_t capacity,
                        size_t item_size) {
    const SlophAllocator *allocator = sloph_context_allocator(project->context);
    if (pointer != NULL)
        allocator->deallocate(allocator->user_data, pointer,
                              capacity * item_size);
}

void sloph_project_free(SlophProject *project) {
    size_t index;
    SlophContext *context;
    const SlophAllocator *allocator;
    if (project == NULL) return;
    context = project->context;
    allocator = sloph_context_allocator(context);
    for (index = 0u; index < project->module_count; ++index) {
        sloph_syntax_module_free(project->modules[index].syntax);
    }
    free_vector(project, project->modules, project->module_capacity,
                sizeof(*project->modules));
    free_vector(project, project->providers, project->provider_capacity,
                sizeof(*project->providers));
    sloph_arena_destroy(&project->arena);
    allocator->deallocate(allocator->user_data, project, sizeof(*project));
}

SlophStatus sloph_project_load(SlophContext *context, const char *path,
                               const SlophProjectOptions *requested,
                               SlophProject **out_project) {
    SlophProjectOptions options;
    SlophProject *project;
    const SlophAllocator *allocator;
    SlophStatus status;
    SlophHost fallback;
    if (context == NULL || path == NULL || out_project == NULL)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    *out_project = NULL;
    options = requested != NULL ? *requested : sloph_project_options_default();
    if (options.source_version > 1u) return SLOPH_STATUS_INVALID_ARGUMENT;
    if (options.host == NULL) {
        fallback = sloph_posix_host();
        options.host = &fallback;
    }
    if (!options.target_is_set) {
        status = sloph_compiler_target_host(options.host, &options.target);
        if (status != SLOPH_STATUS_OK)
            return sloph_project_diag(context, status,
                "compiler.target.unsupported_host", "environment",
                "the experimental compiler supports only Linux or Darwin on AMD64 or ARM64", "{}");
    }
    status = sloph_project_validate_target(context, options.target);
    if (status != SLOPH_STATUS_OK) return status;
    allocator = sloph_context_allocator(context);
    project = allocator->allocate(allocator->user_data, sizeof(*project));
    if (project == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    memset(project, 0, sizeof(*project));
    project->context = context;
    project->target = options.target;
    sloph_arena_init(&project->arena, context,
                     sloph_context_limits(context)->project_bytes, 4096u);
    status = sloph_project_load_manifest(project, options.host, path);
    if (status == SLOPH_STATUS_OK)
        status = sloph_project_load_sources(project, &options);
    if (status == SLOPH_STATUS_OK && options.source_version == 1u)
        status = sloph_project_load_packages(project, &options);
    if (status == SLOPH_STATUS_OK)
        status = sloph_project_order_modules(project);
    if (status == SLOPH_STATUS_OK)
        status = sloph_project_load_providers(project, &options);
    if (status != SLOPH_STATUS_OK) {
        sloph_project_free(project);
        return status;
    }
    *out_project = project;
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_project_manifest(const SlophProject *project,
                                   SlophProjectManifestView *out_manifest) {
    if (project == NULL || out_manifest == NULL)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    out_manifest->path = project->manifest.path;
    out_manifest->package = project->manifest.package;
    out_manifest->source_root = project->manifest.source_root;
    out_manifest->entry = project->manifest.entry;
    out_manifest->dependency_count = project->manifest.dependency_count;
    out_manifest->dependencies = (const char *const *)project->manifest.dependencies;
    return SLOPH_STATUS_OK;
}

size_t sloph_project_module_count(const SlophProject *project) {
    return project != NULL ? project->module_count : 0u;
}

SlophStatus sloph_project_module(const SlophProject *project, size_t index,
                                 SlophProjectModuleView *out_module) {
    const SlophProjectModule *module;
    if (project == NULL || out_module == NULL || index >= project->module_count)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    module = &project->modules[index];
    out_module->name = module->name; out_module->path = module->path;
    out_module->syntax = module->syntax;
    out_module->import_count = module->import_count;
    out_module->imports = (const char *const *)module->imports;
    out_module->bundled = module->bundled;
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_project_find_module(const SlophProject *project,
                                      const char *name,
                                      SlophProjectModuleView *out_module) {
    size_t index;
    if (project == NULL || name == NULL || out_module == NULL)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    for (index = 0u; index < project->module_count; ++index)
        if (strcmp(project->modules[index].name, name) == 0)
            return sloph_project_module(project, index, out_module);
    return SLOPH_STATUS_INVALID_ARGUMENT;
}

size_t sloph_project_provider_count(const SlophProject *project) {
    return project != NULL ? project->provider_count : 0u;
}

SlophStatus sloph_project_provider(const SlophProject *project, size_t index,
                                   SlophProviderSourceView *out_provider) {
    const SlophProjectProvider *provider;
    if (project == NULL || out_provider == NULL || index >= project->provider_count)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    provider = &project->providers[index];
    out_provider->module = provider->module; out_provider->root = provider->root;
    out_provider->bindings_path = provider->bindings_path;
    out_provider->source_count = provider->source_count;
    out_provider->sources = (const char *const *)provider->sources;
    return SLOPH_STATUS_OK;
}
