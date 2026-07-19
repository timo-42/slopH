#include "project_internal.h"
#include "yyjson.h"

#include <string.h>
#include <sys/stat.h>

static int local_name(const char *name) {
    return name != NULL && *name != '\0' && strchr(name, '/') == NULL &&
           strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static int regular_not_symlink(const char *path) {
    struct stat info;
    if (lstat(path, &info) != 0 || S_ISLNK(info.st_mode)) return 0;
    return S_ISREG(info.st_mode);
}

static SlophStatus provider_error(SlophProject *project, const char *code,
                                  const char *message) {
    return sloph_project_diag(project->context, SLOPH_STATUS_INVALID_ARGUMENT,
                              code, "project", message, "{}");
}

static SlophStatus grow_providers(SlophProject *project) {
    const SlophAllocator *allocator = sloph_context_allocator(project->context);
    size_t capacity = project->provider_capacity == 0u
        ? 4u : project->provider_capacity * 2u;
    SlophProjectProvider *grown = allocator->resize(allocator->user_data,
        project->providers,
        project->provider_capacity * sizeof(*project->providers),
        capacity * sizeof(*project->providers));
    if (grown == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    project->providers = grown; project->provider_capacity = capacity;
    return SLOPH_STATUS_OK;
}

static int object_exact(yyjson_val *object, const char *const *keys,
                        size_t count) {
    yyjson_obj_iter iterator;
    yyjson_val *key;
    size_t index;
    unsigned int seen = 0u;
    if (!yyjson_is_obj(object) || yyjson_obj_size(object) != count) return 0;
    iterator = yyjson_obj_iter_with(object);
    while ((key = yyjson_obj_iter_next(&iterator)) != NULL) {
        const char *name = yyjson_get_str(key);
        int found = 0;
        for (index = 0u; index < count; ++index)
            if (strcmp(name, keys[index]) == 0) {
                unsigned int bit = 1u << index;
                if ((seen & bit) != 0u) return 0;
                seen |= bit; found = 1; break;
            }
        if (!found) return 0;
    }
    return seen == (1u << count) - 1u;
}

static SlophStatus validate_bindings(SlophProject *project,
                                     const SlophProjectOptions *options,
                                     SlophProjectProvider *provider) {
    SlophOwnedBytes bytes;
    yyjson_doc *document;
    yyjson_val *root;
    yyjson_arr_iter iterator;
    yyjson_val *binding;
    SlophStatus status = options->host->read_file(options->host->user_data,
        project->context, provider->bindings_path,
        sloph_context_limits(project->context)->input_bytes, &bytes);
    if (status != SLOPH_STATUS_OK) return status;
    document = yyjson_read((char *)bytes.data, bytes.length, 0u);
    options->host->release_bytes(options->host->user_data, project->context,
                                 &bytes);
    if (document == NULL)
        return provider_error(project, "project.foreign_binding.syntax",
                              "invalid foreign binding metadata");
    root = yyjson_doc_get_root(document);
    if (!yyjson_is_arr(root)) {
        yyjson_doc_free(document);
        return provider_error(project, "project.foreign_binding.shape",
                              "foreign binding metadata must be an array");
    }
    iterator = yyjson_arr_iter_with(root);
    while ((binding = yyjson_arr_iter_next(&iterator)) != NULL) {
        yyjson_val *provider_name;
        yyjson_val *header;
        char *header_path;
        if (!yyjson_is_obj(binding) ||
            !yyjson_is_str(provider_name = yyjson_obj_get(binding, "provider")) ||
            !yyjson_is_str(header = yyjson_obj_get(binding, "header")) ||
            !yyjson_is_str(yyjson_obj_get(binding, "identity")) ||
            !yyjson_is_str(yyjson_obj_get(binding, "symbol")) ||
            strcmp(yyjson_get_str(provider_name), provider->module) != 0) {
            yyjson_doc_free(document);
            return provider_error(project, "project.provider.binding",
                                  "foreign binding names a different provider");
        }
        if (!local_name(yyjson_get_str(header)) ||
            sloph_project_join_path(project, provider->root,
                yyjson_get_str(header), &header_path) != SLOPH_STATUS_OK ||
            !regular_not_symlink(header_path)) {
            yyjson_doc_free(document);
            return provider_error(project, "project.provider.header",
                                  "native provider header is missing or invalid");
        }
    }
    yyjson_doc_free(document);
    return SLOPH_STATUS_OK;
}

static SlophStatus load_provider(SlophProject *project,
                                 const SlophProjectOptions *options,
                                 SlophProjectModule *module) {
    static const char *const keys[] = {"format", "module", "bindings", "sources"};
    static const char *const legacy_keys[] = {"format", "module", "bindings", "libraries"};
    char *root_path;
    char *manifest_path;
    SlophOwnedBytes bytes;
    yyjson_doc *document;
    yyjson_val *root;
    yyjson_val *sources;
    yyjson_val *format_value;
    yyjson_arr_iter iterator;
    yyjson_val *source;
    SlophProjectProvider provider;
    SlophStatus status;
    size_t module_path_length = strlen(module->path);
    if (module_path_length < 6u) return SLOPH_STATUS_INTERNAL_ERROR;
    status = sloph_arena_copy_string(&project->arena, module->path,
        module_path_length - 6u, &root_path);
    if (status == SLOPH_STATUS_OK)
        status = sloph_project_join_path(project, root_path, "provider.json",
                                         &manifest_path);
    if (status != SLOPH_STATUS_OK) return status;
    if (!sloph_project_regular_file(manifest_path)) return SLOPH_STATUS_OK;
    status = options->host->read_file(options->host->user_data, project->context,
        manifest_path, 65536u, &bytes);
    if (status != SLOPH_STATUS_OK) return status;
    document = yyjson_read((char *)bytes.data, bytes.length, 0u);
    options->host->release_bytes(options->host->user_data, project->context,
                                 &bytes);
    if (document == NULL)
        return provider_error(project, "project.provider.syntax",
                              "invalid native provider metadata");
    root = yyjson_doc_get_root(document);
    format_value = yyjson_is_obj(root) ? yyjson_obj_get(root, "format") : NULL;
    sources = yyjson_is_obj(root) ? yyjson_obj_get(root, "sources") : NULL;
    if ((!object_exact(root, keys, 4u) &&
         !object_exact(root, legacy_keys, 4u)) ||
        !yyjson_is_int(format_value) ||
        (yyjson_get_sint(format_value) != 0 && yyjson_get_sint(format_value) != 1) ||
        !yyjson_is_str(yyjson_obj_get(root, "module")) ||
        strcmp(yyjson_get_str(yyjson_obj_get(root, "module")), module->name) != 0 ||
        !yyjson_is_str(yyjson_obj_get(root, "bindings")) ||
        (yyjson_get_sint(format_value) == 1 &&
         (!yyjson_is_arr(sources) || yyjson_arr_size(sources) == 0u)) ||
        (yyjson_get_sint(format_value) == 0 &&
         !yyjson_is_arr(yyjson_obj_get(root, "libraries")))) {
        yyjson_doc_free(document);
        return provider_error(project, "project.provider.shape",
            "native provider metadata has missing or unknown fields");
    }
    memset(&provider, 0, sizeof(provider));
    provider.module = module->name; provider.root = root_path;
    if (!local_name(yyjson_get_str(yyjson_obj_get(root, "bindings"))) ||
        sloph_project_join_path(project, root_path,
            yyjson_get_str(yyjson_obj_get(root, "bindings")),
            &provider.bindings_path) != SLOPH_STATUS_OK ||
        !regular_not_symlink(provider.bindings_path)) {
        yyjson_doc_free(document);
        return provider_error(project, "project.provider.shape",
                              "provider bindings must be a local filename");
    }
    provider.source_count = yyjson_get_sint(format_value) == 1
        ? yyjson_arr_size(sources) : 0u;
    if (provider.source_count == 0u) {
        yyjson_doc_free(document);
        status = validate_bindings(project, options, &provider);
        if (status != SLOPH_STATUS_OK) return status;
        if (project->provider_count == project->provider_capacity &&
            (status = grow_providers(project)) != SLOPH_STATUS_OK) return status;
        project->providers[project->provider_count++] = provider;
        return SLOPH_STATUS_OK;
    }
    status = sloph_arena_allocate(&project->arena,
        provider.source_count * sizeof(char *), _Alignof(char *),
        (void **)&provider.sources);
    if (status != SLOPH_STATUS_OK) { yyjson_doc_free(document); return status; }
    memset(provider.sources, 0, provider.source_count * sizeof(char *));
    iterator = yyjson_arr_iter_with(sources);
    while ((source = yyjson_arr_iter_next(&iterator)) != NULL) {
        const char *name;
        char *path;
        size_t index = 0u;
        size_t length;
        if (!yyjson_is_str(source) || !local_name(name = yyjson_get_str(source)))
            goto invalid_source;
        length = strlen(name);
        if (!(length > 2u && (strcmp(name + length - 2u, ".c") == 0 ||
                              strcmp(name + length - 2u, ".S") == 0)))
            goto invalid_source;
        for (index = 0u; index < provider.source_count &&
                         provider.sources[index] != NULL; ++index)
            if (strcmp(provider.sources[index], name) == 0) goto invalid_source;
        if (sloph_project_join_path(project, root_path, name, &path) != SLOPH_STATUS_OK ||
            !regular_not_symlink(path)) goto invalid_source;
        provider.sources[index] = path;
    }
    yyjson_doc_free(document);
    status = validate_bindings(project, options, &provider);
    if (status != SLOPH_STATUS_OK) return status;
    if (project->provider_count == project->provider_capacity &&
        (status = grow_providers(project)) != SLOPH_STATUS_OK) return status;
    project->providers[project->provider_count++] = provider;
    return SLOPH_STATUS_OK;
invalid_source:
    yyjson_doc_free(document);
    return provider_error(project, "project.provider.shape",
        "provider sources must be unique local C or assembly filenames");
}

SlophStatus sloph_project_load_providers(SlophProject *project,
                                         const SlophProjectOptions *options) {
    size_t index;
    SlophStatus status = SLOPH_STATUS_OK;
    for (index = 0u; status == SLOPH_STATUS_OK && index < project->module_count;
         ++index) {
        if (project->modules[index].bundled)
            status = load_provider(project, options, &project->modules[index]);
    }
    return status;
}
