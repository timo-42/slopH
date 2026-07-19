#include "project_internal.h"
#include "yyjson.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef struct PathList {
    SlophProject *project;
    char **items;
    size_t count;
    size_t capacity;
} PathList;

static SlophStatus grow_paths(PathList *list) {
    const SlophAllocator *allocator = sloph_context_allocator(list->project->context);
    size_t old_size = list->capacity * sizeof(*list->items);
    size_t capacity = list->capacity == 0u ? 16u : list->capacity * 2u;
    char **grown = allocator->resize(allocator->user_data, list->items, old_size,
                                     capacity * sizeof(*list->items));
    if (grown == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    list->items = grown; list->capacity = capacity;
    return SLOPH_STATUS_OK;
}

static int compare_paths(const void *left, const void *right) {
    return strcmp(*(char *const *)left, *(char *const *)right);
}

static SlophStatus collect_sources(PathList *list, const char *directory) {
    DIR *stream = opendir(directory);
    struct dirent *entry;
    SlophStatus status = SLOPH_STATUS_OK;
    if (stream == NULL)
        return sloph_project_diag(list->project->context, SLOPH_STATUS_IO_ERROR,
            "project.source.io", "project", "could not enumerate source-root", "{}");
    while ((entry = readdir(stream)) != NULL) {
        char *path;
        struct stat info;
        size_t length;
        if (strcmp(entry->d_name, ".") == 0 || strcmp(entry->d_name, "..") == 0)
            continue;
        status = sloph_project_join_path(list->project, directory, entry->d_name,
                                         &path);
        if (status != SLOPH_STATUS_OK) break;
        if (stat(path, &info) != 0) { status = SLOPH_STATUS_IO_ERROR; break; }
        if (S_ISDIR(info.st_mode)) {
            status = collect_sources(list, path);
            if (status != SLOPH_STATUS_OK) break;
        } else if (S_ISREG(info.st_mode)) {
            length = strlen(entry->d_name);
            if (length < 6u || strcmp(entry->d_name + length - 6u, ".sloph") != 0)
                continue;
            if (list->project->module_count + list->count >=
                sloph_context_limits(list->project->context)->project_files) {
                status = sloph_project_diag(list->project->context,
                    SLOPH_STATUS_LIMIT_EXCEEDED, "project.files.limit", "project",
                    "project source-file limit exceeded", "{}");
                break;
            }
            if (list->count == list->capacity &&
                (status = grow_paths(list)) != SLOPH_STATUS_OK) break;
            list->items[list->count++] = path;
        }
    }
    (void)closedir(stream);
    return status;
}

static SlophStatus expected_application_name(SlophProject *project,
                                             const char *path, char **out) {
    size_t root_length = strlen(project->manifest.source_root);
    const char *relative = path + root_length;
    size_t package_length = strlen(project->manifest.package);
    size_t relative_length;
    size_t capacity;
    char *name;
    size_t index;
    size_t position;
    if (*relative == '/') ++relative;
    relative_length = strlen(relative);
    if (relative_length < 6u) return SLOPH_STATUS_INTERNAL_ERROR;
    relative_length -= 6u;
    capacity = package_length + 2u + relative_length * 2u + 1u;
    if (sloph_arena_allocate(&project->arena, capacity, 1u,
                             (void **)&name) != SLOPH_STATUS_OK)
        return SLOPH_STATUS_OUT_OF_MEMORY;
    memcpy(name, project->manifest.package, package_length);
    name[package_length] = ':'; name[package_length + 1u] = ':';
    position = package_length + 2u;
    for (index = 0u; index < relative_length; ++index) {
        if (relative[index] == '/') {
            name[position++] = ':'; name[position++] = ':';
        } else name[position++] = relative[index];
    }
    name[position] = '\0';
    *out = name;
    return SLOPH_STATUS_OK;
}

SlophStatus sloph_project_load_sources(SlophProject *project,
                                       const SlophProjectOptions *options) {
    PathList list;
    SlophStatus status;
    size_t index;
    const SlophAllocator *allocator = sloph_context_allocator(project->context);
    memset(&list, 0, sizeof(list)); list.project = project;
    status = collect_sources(&list, project->manifest.source_root);
    if (status == SLOPH_STATUS_OK) qsort(list.items, list.count,
                                         sizeof(*list.items), compare_paths);
    for (index = 0u; status == SLOPH_STATUS_OK && index < list.count; ++index) {
        char *expected;
        status = expected_application_name(project, list.items[index], &expected);
        if (status == SLOPH_STATUS_OK)
            status = sloph_project_add_source_file(project, options,
                list.items[index], expected, 0);
    }
    if (list.items != NULL) allocator->deallocate(allocator->user_data, list.items,
        list.capacity * sizeof(*list.items));
    if (status == SLOPH_STATUS_OK && list.count == 0u)
        status = sloph_project_diag(project->context, SLOPH_STATUS_INVALID_ARGUMENT,
            "project.module.none", "project", "source-root contains no .sloph modules", "{}");
    return status;
}

/* Package loading is below source loading so both use the same deterministic
 * walker and parser. */
typedef struct PackageState {
    SlophProject *project;
    const SlophProjectOptions *options;
    const char *root;
    char **complete;
    size_t complete_count;
    size_t complete_capacity;
    char **active;
    size_t active_count;
    size_t active_capacity;
} PackageState;

static int string_in(char **items, size_t count, const char *name) {
    size_t index;
    for (index = 0u; index < count; ++index)
        if (strcmp(items[index], name) == 0) return 1;
    return 0;
}

static SlophStatus grow_package_names(PackageState *state, char ***items,
                                      size_t *capacity, size_t count) {
    const SlophAllocator *allocator =
        sloph_context_allocator(state->project->context);
    size_t maximum = sloph_context_limits(state->project->context)->project_bytes /
                     (2u * sizeof(char *));
    size_t next;
    char **grown;
    if (count < *capacity) return SLOPH_STATUS_OK;
    if (*capacity == 0u) next = 16u;
    else if (*capacity > SIZE_MAX / 2u) next = maximum + 1u;
    else next = *capacity * 2u;
    if (next > maximum) next = maximum;
    if (next <= *capacity || next > SIZE_MAX / sizeof(char *))
        return sloph_project_diag(state->project->context,
            SLOPH_STATUS_LIMIT_EXCEEDED, "project.dependency.limit", "project",
            "bundled dependency count exceeds the configured project limit", "{}");
    grown = allocator->resize(allocator->user_data, *items,
        *capacity * sizeof(char *), next * sizeof(char *));
    if (grown == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    *items = grown;
    *capacity = next;
    return SLOPH_STATUS_OK;
}

static SlophStatus package_visit(PackageState *state, const char *package) {
    char *package_root;
    char *manifest_path;
    char *source_root;
    SlophOwnedBytes bytes;
    yyjson_doc *document;
    yyjson_val *root;
    yyjson_val *dependencies;
    yyjson_arr_iter iterator;
    yyjson_val *item;
    SlophStatus status;
    PathList paths;
    size_t index;
    if (string_in(state->complete, state->complete_count, package)) return SLOPH_STATUS_OK;
    if (string_in(state->active, state->active_count, package))
        return sloph_project_diag(state->project->context, SLOPH_STATUS_INVALID_ARGUMENT,
            "project.dependency.cycle", "project",
            "bundled dependency graph contains a cycle", "{}");
    status = sloph_project_join_path(state->project, state->root, package, &package_root);
    if (status == SLOPH_STATUS_OK)
        status = sloph_project_join_path(state->project, package_root,
                                         "library.json", &manifest_path);
    if (status != SLOPH_STATUS_OK) return status;
    if (!sloph_project_regular_file(manifest_path))
        return sloph_project_diag(state->project->context, SLOPH_STATUS_INVALID_ARGUMENT,
            "project.dependency.missing", "project",
            "bundled dependency does not exist", "{}");
    status = state->options->host->read_file(state->options->host->user_data,
        state->project->context, manifest_path, 65536u, &bytes);
    if (status != SLOPH_STATUS_OK) return status;
    document = yyjson_read((char *)bytes.data, bytes.length, 0u);
    state->options->host->release_bytes(state->options->host->user_data,
        state->project->context, &bytes);
    if (document == NULL) goto invalid;
    root = yyjson_doc_get_root(document);
    dependencies = yyjson_is_obj(root) ? yyjson_obj_get(root, "dependencies") : NULL;
    if (!yyjson_is_obj(root) || yyjson_obj_size(root) != 3u ||
        !yyjson_is_int(yyjson_obj_get(root, "format")) ||
        yyjson_get_sint(yyjson_obj_get(root, "format")) != 0 ||
        !yyjson_is_str(yyjson_obj_get(root, "package")) ||
        strcmp(yyjson_get_str(yyjson_obj_get(root, "package")), package) != 0 ||
        !yyjson_is_arr(dependencies)) goto invalid_document;
    for (index = 0u; index < yyjson_arr_size(dependencies); ++index) {
        yyjson_val *candidate = yyjson_arr_get(dependencies, index);
        size_t previous;
        if (!yyjson_is_str(candidate) ||
            !sloph_project_lower_segment(yyjson_get_str(candidate)))
            goto invalid_document;
        for (previous = 0u; previous < index; ++previous) {
            yyjson_val *earlier = yyjson_arr_get(dependencies, previous);
            if (strcmp(yyjson_get_str(earlier), yyjson_get_str(candidate)) == 0)
                goto invalid_document;
        }
    }
    status = grow_package_names(state, &state->active,
                                &state->active_capacity, state->active_count);
    if (status != SLOPH_STATUS_OK) { yyjson_doc_free(document); return status; }
    state->active[state->active_count++] = (char *)package;
    iterator = yyjson_arr_iter_with(dependencies);
    while ((item = yyjson_arr_iter_next(&iterator)) != NULL) {
        const char *dependency;
        if (!yyjson_is_str(item) ||
            !sloph_project_lower_segment(dependency = yyjson_get_str(item)))
            goto invalid_document;
        status = package_visit(state, dependency);
        if (status != SLOPH_STATUS_OK) { yyjson_doc_free(document); return status; }
    }
    --state->active_count;
    yyjson_doc_free(document);
    {
        char *owned_package;
        status = sloph_project_arena_string(state->project, package,
                                            &owned_package);
        if (status != SLOPH_STATUS_OK) return status;
        status = grow_package_names(state, &state->complete,
                                    &state->complete_capacity,
                                    state->complete_count);
        if (status != SLOPH_STATUS_OK) return status;
        state->complete[state->complete_count++] = owned_package;
    }
    status = sloph_project_join_path(state->project, package_root, "src", &source_root);
    if (status != SLOPH_STATUS_OK) return status;
    memset(&paths, 0, sizeof(paths)); paths.project = state->project;
    status = collect_sources(&paths, source_root);
    if (status == SLOPH_STATUS_OK) qsort(paths.items, paths.count,
        sizeof(*paths.items), compare_paths);
    for (index = 0u; status == SLOPH_STATUS_OK && index < paths.count; ++index) {
        const char *relative = paths.items[index] + strlen(source_root) + 1u;
        size_t relative_length = strlen(relative) - 6u;
        size_t package_length = strlen(package);
        char *expected;
        size_t position;
        int root_module = relative_length == 4u &&
                          strncmp(relative, "root", 4u) == 0;
        size_t size = root_module ? package_length + 1u
            : package_length + 2u + relative_length * 2u + 1u;
        status = sloph_arena_allocate(&state->project->arena, size, 1u,
                                      (void **)&expected);
        if (status != SLOPH_STATUS_OK) break;
        memcpy(expected, package, package_length); position = package_length;
        if (!root_module) {
            expected[position++] = ':'; expected[position++] = ':';
            while (relative_length-- != 0u) {
                char value = *relative++;
                if (value == '/') { expected[position++] = ':'; expected[position++] = ':'; }
                else expected[position++] = value;
            }
        }
        expected[position] = '\0';
        status = sloph_project_add_source_file(state->project, state->options,
            paths.items[index], expected, 1);
    }
    if (paths.items != NULL) {
        const SlophAllocator *allocator = sloph_context_allocator(state->project->context);
        allocator->deallocate(allocator->user_data, paths.items,
                              paths.capacity * sizeof(*paths.items));
    }
    return status;
invalid_document:
    yyjson_doc_free(document);
invalid:
    return sloph_project_diag(state->project->context, SLOPH_STATUS_INVALID_ARGUMENT,
        "project.dependency.manifest", "project",
        "invalid bundled dependency manifest", "{}");
}

SlophStatus sloph_project_load_packages(SlophProject *project,
                                        const SlophProjectOptions *options) {
    PackageState state;
    const SlophAllocator *allocator;
    SlophStatus status;
    size_t index;
    if (options->libraries_root == NULL) return SLOPH_STATUS_OK;
    memset(&state, 0, sizeof(state)); state.project = project;
    state.options = options; state.root = options->libraries_root;
    status = package_visit(&state, "prelude");
    for (index = 0u; status == SLOPH_STATUS_OK &&
         index < project->manifest.dependency_count; ++index)
        status = package_visit(&state, project->manifest.dependencies[index]);
    allocator = sloph_context_allocator(project->context);
    if (state.complete != NULL)
        allocator->deallocate(allocator->user_data, state.complete,
                              state.complete_capacity * sizeof(char *));
    if (state.active != NULL)
        allocator->deallocate(allocator->user_data, state.active,
                              state.active_capacity * sizeof(char *));
    return status;
}
