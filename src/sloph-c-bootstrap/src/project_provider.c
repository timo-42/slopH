#include "project_internal.h"
#include "yyjson.h"

#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

static int local_name(const char *name) {
    const unsigned char *cursor = (const unsigned char *)name;
    if (cursor == NULL || *cursor == '\0' || strcmp(name, ".") == 0 ||
        strcmp(name, "..") == 0) return 0;
    for (; *cursor != '\0'; ++cursor)
        if (!( (*cursor >= 'A' && *cursor <= 'Z') ||
               (*cursor >= 'a' && *cursor <= 'z') ||
               (*cursor >= '0' && *cursor <= '9') || *cursor == '_' ||
               *cursor == '-' || *cursor == '.')) return 0;
    return 1;
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

static int nonempty_string(yyjson_val *value) {
    return yyjson_is_str(value) && yyjson_get_len(value) != 0u;
}

static int string_array(yyjson_val *value) {
    yyjson_arr_iter iterator;
    yyjson_val *item;
    if (!yyjson_is_arr(value)) return 0;
    iterator = yyjson_arr_iter_with(value);
    while ((item = yyjson_arr_iter_next(&iterator)) != NULL)
        if (!yyjson_is_str(item)) return 0;
    return 1;
}

static int string_object(yyjson_val *value) {
    yyjson_obj_iter iterator;
    yyjson_val *key;
    if (!yyjson_is_obj(value)) return 0;
    iterator = yyjson_obj_iter_with(value);
    while ((key = yyjson_obj_iter_next(&iterator)) != NULL) {
        yyjson_val *item = yyjson_obj_iter_get_val(key);
        if (!yyjson_is_str(key) || !yyjson_is_str(item)) return 0;
    }
    return 1;
}

typedef struct BindingTypeParser {
    const unsigned char *text;
    size_t length;
    size_t position;
} BindingTypeParser;

static void binding_type_space(BindingTypeParser *parser) {
    while (parser->position < parser->length) {
        unsigned char byte = parser->text[parser->position];
        if (byte != ' ' && byte != '\t' && byte != '\r' && byte != '\n') break;
        ++parser->position;
    }
}

static int binding_type_parse(BindingTypeParser *parser, size_t depth) {
    size_t name_start, name_end, segment_start;
    int arguments = 0;
    if (depth > 256u) return 0;
    binding_type_space(parser);
    name_start = parser->position;
    segment_start = name_start;
    while (parser->position < parser->length) {
        unsigned char byte = parser->text[parser->position];
        if ((byte >= 'A' && byte <= 'Z') || (byte >= 'a' && byte <= 'z') ||
            (byte >= '0' && byte <= '9') || byte == '_') {
            ++parser->position;
        } else if (byte == ':' && parser->position + 1u < parser->length &&
                   parser->text[parser->position + 1u] == ':') {
            if (parser->position == segment_start) return 0;
            parser->position += 2u; segment_start = parser->position;
        } else break;
    }
    name_end = parser->position;
    if (name_end == name_start || name_end == segment_start) return 0;
    binding_type_space(parser);
    if (parser->position < parser->length &&
        parser->text[parser->position] == '[') {
        ++parser->position;
        for (;;) {
            if (!binding_type_parse(parser, depth + 1u)) return 0;
            ++arguments;
            binding_type_space(parser);
            if (parser->position < parser->length &&
                parser->text[parser->position] == ',') {
                ++parser->position; continue;
            }
            if (parser->position >= parser->length ||
                parser->text[parser->position] != ']') return 0;
            ++parser->position; break;
        }
    }
    if (arguments != 0 &&
        ((name_end - name_start == 3u &&
          memcmp(parser->text + name_start, "Int", 3u) == 0) ||
         (name_end - name_start == 5u &&
          memcmp(parser->text + name_start, "Bytes", 5u) == 0))) return 0;
    return 1;
}

static int binding_type(yyjson_val *value) {
    BindingTypeParser parser;
    if (!nonempty_string(value)) return 0;
    parser.text = (const unsigned char *)yyjson_get_str(value);
    parser.length = yyjson_get_len(value);
    parser.position = 0u;
    if (!binding_type_parse(&parser, 0u)) return 0;
    binding_type_space(&parser);
    return parser.position == parser.length;
}

static int valid_adapter(yyjson_val *adapter) {
    static const char *const keys[] = {
        "kind", "arguments", "result", "sloph_parameters", "sloph_result"
    };
    yyjson_val *parameters;
    yyjson_arr_iter iterator;
    yyjson_val *parameter;
    if (!object_exact(adapter, keys, 5u) ||
        !nonempty_string(yyjson_obj_get(adapter, "kind")) ||
        !string_array(yyjson_obj_get(adapter, "arguments")) ||
        !nonempty_string(yyjson_obj_get(adapter, "result")) ||
        !yyjson_is_arr(parameters = yyjson_obj_get(adapter, "sloph_parameters")) ||
        !binding_type(yyjson_obj_get(adapter, "sloph_result"))) return 0;
    iterator = yyjson_arr_iter_with(parameters);
    while ((parameter = yyjson_arr_iter_next(&iterator)) != NULL)
        if (!binding_type(parameter)) return 0;
    return 1;
}

static int valid_binding_shape(yyjson_val *binding, int *adapter_error) {
    static const char *const required[] = {
        "identity", "symbol", "c_parameters", "c_result", "provider",
        "requires", "effects", "facts", "provenance", "header"
    };
    static const char *const with_adapter[] = {
        "identity", "symbol", "c_parameters", "c_result", "provider",
        "requires", "effects", "facts", "provenance", "header", "adapter"
    };
    yyjson_val *adapter;
    *adapter_error = 0;
    if (!yyjson_is_obj(binding) ||
        !(object_exact(binding, required, 10u) ||
          object_exact(binding, with_adapter, 11u)) ||
        !nonempty_string(yyjson_obj_get(binding, "identity")) ||
        !nonempty_string(yyjson_obj_get(binding, "symbol")) ||
        !string_array(yyjson_obj_get(binding, "c_parameters")) ||
        !nonempty_string(yyjson_obj_get(binding, "c_result")) ||
        !nonempty_string(yyjson_obj_get(binding, "provider")) ||
        !string_array(yyjson_obj_get(binding, "requires")) ||
        !string_array(yyjson_obj_get(binding, "effects")) ||
        !string_object(yyjson_obj_get(binding, "facts")) ||
        !nonempty_string(yyjson_obj_get(binding, "provenance")) ||
        !nonempty_string(yyjson_obj_get(binding, "header"))) return 0;
    adapter = yyjson_obj_get(binding, "adapter");
    if (adapter != NULL && !valid_adapter(adapter)) {
        *adapter_error = 1; return 0;
    }
    return 1;
}

static SlophStatus validate_bindings(SlophProject *project,
                                     const SlophProjectOptions *options,
                                     SlophProjectProvider *provider) {
    SlophOwnedBytes bytes;
    yyjson_doc *document;
    yyjson_val *root;
    yyjson_arr_iter iterator;
    yyjson_val *binding;
    yyjson_val **seen = NULL;
    size_t seen_count = 0u;
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
    if (yyjson_arr_size(root) != 0u) {
        seen = calloc(yyjson_arr_size(root), sizeof(*seen));
        if (seen == NULL) { yyjson_doc_free(document); return SLOPH_STATUS_OUT_OF_MEMORY; }
    }
    iterator = yyjson_arr_iter_with(root);
    while ((binding = yyjson_arr_iter_next(&iterator)) != NULL) {
        yyjson_val *provider_name;
        yyjson_val *header;
        char *header_path;
        int adapter_error = 0;
        size_t previous;
        if (!valid_binding_shape(binding, &adapter_error)) {
            free(seen);
            yyjson_doc_free(document);
            return provider_error(project,
                adapter_error ? "project.foreign_binding.adapter" :
                                "project.foreign_binding.shape",
                adapter_error ? "invalid foreign adapter metadata" :
                                "foreign binding has missing or unknown fields");
        }
        provider_name = yyjson_obj_get(binding, "provider");
        header = yyjson_obj_get(binding, "header");
        if (strcmp(yyjson_get_str(provider_name), provider->module) != 0) {
            free(seen);
            yyjson_doc_free(document);
            return provider_error(project, "project.provider.binding",
                                  "foreign binding names a different provider");
        }
        for (previous = 0u; previous < seen_count; ++previous)
            if (strcmp(yyjson_get_str(yyjson_obj_get(seen[previous], "identity")),
                       yyjson_get_str(yyjson_obj_get(binding, "identity"))) == 0) {
                free(seen); yyjson_doc_free(document);
                return provider_error(project, "project.foreign_binding.shape",
                                      "foreign binding identities must be unique");
            }
        seen[seen_count++] = binding;
        if (!local_name(yyjson_get_str(header)) ||
            sloph_project_join_path(project, provider->root,
                yyjson_get_str(header), &header_path) != SLOPH_STATUS_OK ||
            !regular_not_symlink(header_path)) {
            free(seen); yyjson_doc_free(document);
            return provider_error(project, "project.provider.header",
                                  "native provider header is missing or invalid");
        }
    }
    free(seen);
    yyjson_doc_free(document);
    return SLOPH_STATUS_OK;
}

static SlophStatus load_provider(SlophProject *project,
                                 const SlophProjectOptions *options,
                                 SlophProjectModule *module) {
    static const char *const keys[] = {"format", "module", "bindings", "sources"};
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
    if (!object_exact(root, keys, 4u) ||
        !yyjson_is_int(format_value) ||
        yyjson_get_sint(format_value) != 1 ||
        !yyjson_is_str(yyjson_obj_get(root, "module")) ||
        strcmp(yyjson_get_str(yyjson_obj_get(root, "module")), module->name) != 0 ||
        !yyjson_is_str(yyjson_obj_get(root, "bindings")) ||
        !yyjson_is_arr(sources) || yyjson_arr_size(sources) == 0u) {
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
    provider.source_count = yyjson_arr_size(sources);
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
        if (sloph_project_join_path(project, root_path, name, &path) != SLOPH_STATUS_OK ||
            !regular_not_symlink(path)) goto invalid_source;
        for (index = 0u; index < provider.source_count &&
                         provider.sources[index] != NULL; ++index)
            if (strcmp(provider.sources[index], path) == 0) goto invalid_source;
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
