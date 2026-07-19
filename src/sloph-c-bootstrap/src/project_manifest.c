#include "project_internal.h"
#include "yyjson.h"

#include <stdio.h>
#include <string.h>

#define SLOPH_MANIFEST_BYTES 65536u

static int known_key(const char *key) {
    return strcmp(key, "format") == 0 || strcmp(key, "package") == 0 ||
           strcmp(key, "source-root") == 0 || strcmp(key, "entry") == 0 ||
           strcmp(key, "dependencies") == 0;
}

static SlophStatus manifest_error(SlophProject *project, const char *code,
                                  const char *message, const char *details) {
    return sloph_project_diag(project->context, SLOPH_STATUS_INVALID_ARGUMENT,
                              code, "project", message, details);
}

static SlophStatus copy_required_string(SlophProject *project, yyjson_val *root,
                                        const char *key, char **out) {
    const char *field = key != NULL ? key : "";
    yyjson_val *value = yyjson_obj_get(root, field);
    const char *text;
    char details[96];
    if (!yyjson_is_str(value) || yyjson_get_len(value) == 0u) {
        (void)snprintf(details, sizeof(details), "{\"field\":\"%s\"}", field);
        return manifest_error(project, "project.manifest.field_type",
                              strcmp(field, "package") == 0
                                ? "manifest field 'package' must be a non-empty string"
                                : strcmp(field, "entry") == 0
                                  ? "manifest field 'entry' must be a non-empty string"
                                  : "manifest field 'source-root' must be a non-empty string",
                              details);
    }
    text = yyjson_get_str(value);
    return sloph_project_arena_string(project, text, out);
}

static int normalized_relative(const char *path) {
    const char *start = path;
    const char *cursor = path;
    if (*path == '\0' || *path == '/') return 0;
    for (;;) {
        if (*cursor == '/' || *cursor == '\0') {
            size_t length = (size_t)(cursor - start);
            if (length == 0u || (length == 1u && start[0] == '.') ||
                (length == 2u && start[0] == '.' && start[1] == '.')) return 0;
            if (*cursor == '\0') break;
            start = cursor + 1;
        }
        ++cursor;
    }
    return 1;
}

static SlophStatus check_object_keys(SlophProject *project, yyjson_val *root) {
    yyjson_obj_iter iter = yyjson_obj_iter_with(root);
    yyjson_val *key;
    const char **seen;
    size_t key_count = yyjson_obj_size(root);
    size_t seen_count = 0u;
    size_t index;
    if (sloph_arena_allocate(&project->arena,
                             (key_count == 0u ? 1u : key_count) * sizeof(*seen),
                             _Alignof(const char *), (void **)&seen) !=
        SLOPH_STATUS_OK) return SLOPH_STATUS_OUT_OF_MEMORY;
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        const char *name = yyjson_get_str(key);
        char details[128];
        for (index = 0u; index < seen_count; ++index) {
            if (strcmp(seen[index], name) == 0) {
                (void)snprintf(details, sizeof(details),
                               "{\"field\":\"%s\"}", name);
                return manifest_error(project, "project.manifest.duplicate",
                                      "manifest contains a duplicate field",
                                      details);
            }
        }
        seen[seen_count++] = name;
    }
    iter = yyjson_obj_iter_with(root);
    while ((key = yyjson_obj_iter_next(&iter)) != NULL) {
        const char *name = yyjson_get_str(key);
        if (!known_key(name)) {
            char details[160];
            (void)snprintf(details, sizeof(details),
                           "{\"fields\":[\"%s\"]}", name);
            return manifest_error(project, "project.manifest.unknown",
                                  "manifest contains unknown fields", details);
        }
    }
    return SLOPH_STATUS_OK;
}

static SlophStatus decode_dependencies(SlophProject *project,
                                       yyjson_val *root) {
    yyjson_val *value = yyjson_obj_get(root, "dependencies");
    yyjson_arr_iter iter;
    yyjson_val *item;
    size_t index;
    if (value == NULL) return SLOPH_STATUS_OK;
    if (!yyjson_is_arr(value)) goto invalid;
    project->manifest.dependency_count = yyjson_arr_size(value);
    if (project->manifest.dependency_count != 0u) {
        SlophStatus status = sloph_arena_allocate(
            &project->arena,
            project->manifest.dependency_count * sizeof(char *),
            _Alignof(char *), (void **)&project->manifest.dependencies);
        if (status != SLOPH_STATUS_OK) return status;
    }
    iter = yyjson_arr_iter_with(value);
    index = 0u;
    while ((item = yyjson_arr_iter_next(&iter)) != NULL) {
        const char *name;
        size_t previous;
        if (!yyjson_is_str(item)) goto invalid;
        name = yyjson_get_str(item);
        if (!sloph_project_lower_segment(name)) goto invalid;
        for (previous = 0u; previous < index; ++previous)
            if (strcmp(project->manifest.dependencies[previous], name) == 0)
                return manifest_error(project, "project.manifest.dependencies",
                    "dependencies must not contain duplicates", "{}");
        if (sloph_project_arena_string(project, name,
             &project->manifest.dependencies[index]) != SLOPH_STATUS_OK)
            return SLOPH_STATUS_OUT_OF_MEMORY;
        ++index;
    }
    return SLOPH_STATUS_OK;
invalid:
    return manifest_error(project, "project.manifest.dependencies",
        "dependencies must be an array of lowercase package names", "{}");
}

SlophStatus sloph_project_load_manifest(SlophProject *project,
                                        const SlophHost *host,
                                        const char *supplied_path) {
    char *manifest_path;
    SlophOwnedBytes bytes;
    SlophStatus status;
    yyjson_doc *document;
    yyjson_read_err error;
    yyjson_val *root;
    yyjson_val *format;
    const char *required[] = {"format", "package", "source-root", "entry"};
    size_t index;
    if (sloph_project_directory(supplied_path)) {
        status = sloph_project_join_path(project, supplied_path, "sloph.json",
                                         &manifest_path);
    } else {
        status = sloph_project_arena_string(project, supplied_path,
                                            &manifest_path);
    }
    if (status != SLOPH_STATUS_OK) return status;
    if (host->read_file == NULL || host->release_bytes == NULL)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    status = host->read_file(host->user_data, project->context, manifest_path,
                             SLOPH_MANIFEST_BYTES + 1u, &bytes);
    if (status != SLOPH_STATUS_OK) return status;
    if (bytes.length > SLOPH_MANIFEST_BYTES) {
        host->release_bytes(host->user_data, project->context, &bytes);
        return manifest_error(project, "project.manifest.limit",
                              "sloph.json exceeds the configured input limit",
                              "{\"configured\":65536}");
    }
    document = yyjson_read_opts((char *)bytes.data, bytes.length, 0u, NULL,
                                &error);
    host->release_bytes(host->user_data, project->context, &bytes);
    if (document == NULL)
        return manifest_error(project, "project.manifest.syntax",
                              "invalid sloph.json", "{}");
    root = yyjson_doc_get_root(document);
    if (!yyjson_is_obj(root)) {
        yyjson_doc_free(document);
        return manifest_error(project, "project.manifest.shape",
                              "manifest must be a JSON object", "{}");
    }
    status = check_object_keys(project, root);
    if (status != SLOPH_STATUS_OK) { yyjson_doc_free(document); return status; }
    for (index = 0u; index < 4u; ++index) {
        if (yyjson_obj_get(root, required[index]) == NULL) {
            char details[128];
            (void)snprintf(details, sizeof(details),
                           "{\"fields\":[\"%s\"]}", required[index]);
            yyjson_doc_free(document);
            return manifest_error(project, "project.manifest.missing",
                "manifest is missing required fields", details);
        }
    }
    format = yyjson_obj_get(root, "format");
    if (!yyjson_is_int(format) || yyjson_get_sint(format) != 1) {
        yyjson_doc_free(document);
        return manifest_error(project, "project.manifest.format",
            "only project manifest format 1 is supported", "{\"format\":0}");
    }
    project->manifest.path = manifest_path;
    status = copy_required_string(project, root, "package",
                                  &project->manifest.package);
    if (status == SLOPH_STATUS_OK)
        status = copy_required_string(project, root, "entry",
                                      &project->manifest.entry);
    if (status == SLOPH_STATUS_OK) {
        char *relative;
        status = copy_required_string(project, root, "source-root", &relative);
        if (status == SLOPH_STATUS_OK && !normalized_relative(relative)) {
            char details[256];
            (void)snprintf(details, sizeof(details),
                           "{\"source_root\":\"%s\"}", relative);
            status = manifest_error(project, "project.manifest.source_root",
                "source-root must be a normalized relative path", details);
        }
        if (status == SLOPH_STATUS_OK) {
            char *slash = strrchr(manifest_path, '/');
            char *directory;
            if (slash == NULL) directory = ".";
            else {
                size_t length = (size_t)(slash - manifest_path);
                status = sloph_arena_copy_string(&project->arena, manifest_path,
                                                  length, &directory);
            }
            if (status == SLOPH_STATUS_OK)
                status = sloph_project_join_path(project, directory, relative,
                                                  &project->manifest.source_root);
            if (status == SLOPH_STATUS_OK &&
                !sloph_project_directory(project->manifest.source_root))
                status = manifest_error(project, "project.manifest.source_root",
                    "source-root is not a directory", "{}");
        }
    }
    if (status == SLOPH_STATUS_OK &&
        !sloph_project_lower_segment(project->manifest.package))
        status = manifest_error(project, "project.manifest.package",
            "package must be one lowercase-leading ASCII identifier segment",
            "{}");
    if (status == SLOPH_STATUS_OK &&
        (!sloph_project_global(project->manifest.entry) ||
         strncmp(project->manifest.entry, project->manifest.package,
                 strlen(project->manifest.package)) != 0 ||
         project->manifest.entry[strlen(project->manifest.package)] != ':'))
        status = manifest_error(project, "project.manifest.entry",
            "entry must be a fully qualified identity in this package", "{}");
    if (status == SLOPH_STATUS_OK) status = decode_dependencies(project, root);
    yyjson_doc_free(document);
    return status;
}
