#include "sloph/native.h"

#include "sloph/backend.h"
#include "yyjson.h"

#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>

#if defined(__APPLE__)
extern char *mkdtemp(char *template_name);
#endif

typedef struct ResolvedProvider {
    const char *module;
    char *root;
    char *bindings_path;
    char **sources;
    size_t source_count;
} ResolvedProvider;

static SlophStatus diagnostic(SlophContext *context, SlophStatus status,
                              const char *code, const char *message,
                              const char *details) {
    (void)sloph_context_add_diagnostic_full(context, code, "environment", message,
                                            details, SLOPH_UNKNOWN_SPAN,
                                            SLOPH_SEVERITY_ERROR);
    return status;
}

static char *copy_text(const char *text) {
    size_t size = strlen(text) + 1u;
    char *copy = malloc(size);
    if (copy != NULL) memcpy(copy, text, size);
    return copy;
}

static char *temporary_template(const char *prefix) {
    const char *base = getenv("TMPDIR");
    size_t base_length, size;
    char *result;
    if (base == NULL || *base == '\0') base = "/tmp";
    base_length = strlen(base);
    while (base_length > 1u && base[base_length - 1u] == '/') --base_length;
    size = base_length + 1u + strlen(prefix) + sizeof("XXXXXX");
    result = malloc(size);
    if (result != NULL)
        (void)snprintf(result, size, "%.*s/%sXXXXXX", (int)base_length,
                       base, prefix);
    return result;
}

static int executable_file(const char *path) {
    struct stat info;
    return stat(path, &info) == 0 && S_ISREG(info.st_mode) &&
           access(path, X_OK) == 0;
}

static int regular_not_symlink(const char *path) {
    struct stat info;
    return lstat(path, &info) == 0 && !S_ISLNK(info.st_mode) &&
           S_ISREG(info.st_mode);
}

static int provider_segment(const char *text, size_t length) {
    size_t index;
    if (length == 0u) return 0;
    for (index = 0u; index < length; ++index) {
        unsigned char byte = (unsigned char)text[index];
        if (!((byte >= 'a' && byte <= 'z') || (byte >= '0' && byte <= '9') ||
              byte == '_')) return 0;
    }
    return 1;
}

static int local_file_name(const char *name) {
    return name != NULL && *name != '\0' && strchr(name, '/') == NULL &&
           strcmp(name, ".") != 0 && strcmp(name, "..") != 0;
}

static const char *native_libraries_root(void) {
    const char *override = getenv("SLOPH_LIBRARIES_ROOT");
    if (override != NULL && *override != '\0') return override;
#ifdef SLOPH_LIBRARIES_ROOT
    return SLOPH_LIBRARIES_ROOT;
#else
    return NULL;
#endif
}

static void clear_resolved_provider(ResolvedProvider *provider) {
    size_t source;
    if (provider == NULL) return;
    for (source = 0u; source < provider->source_count; ++source)
        free(provider->sources[source]);
    free(provider->sources);
    free(provider->bindings_path);
    free(provider->root);
    memset(provider, 0, sizeof(*provider));
}

static void free_resolved_providers(ResolvedProvider *providers, size_t count) {
    size_t index;
    if (providers == NULL) return;
    for (index = 0u; index < count; ++index)
        clear_resolved_provider(&providers[index]);
    free(providers);
}

static int exact_provider_object(yyjson_val *root) {
    static const char *const names[] = {"format", "module", "bindings", "sources"};
    yyjson_obj_iter iterator;
    yyjson_val *key;
    unsigned int seen = 0u;
    size_t index;
    if (!yyjson_is_obj(root) || yyjson_obj_size(root) != 4u) return 0;
    iterator = yyjson_obj_iter_with(root);
    while ((key = yyjson_obj_iter_next(&iterator)) != NULL) {
        const char *name = yyjson_get_str(key);
        for (index = 0u; index < 4u; ++index) {
            if (strcmp(name, names[index]) == 0) {
                unsigned int bit = 1u << index;
                if ((seen & bit) != 0u) return 0;
                seen |= bit; break;
            }
        }
        if (index == 4u) return 0;
    }
    return seen == 15u;
}

static SlophStatus resolve_provider(SlophContext *context, const char *identity,
                                    ResolvedProvider *out) {
    const char *libraries = native_libraries_root();
    const char *cursor = identity, *separator;
    size_t root_length, position = 0u, identity_length = strlen(identity);
    char *manifest = NULL;
    FILE *stream = NULL;
    long file_size;
    char *data = NULL;
    yyjson_doc *document = NULL;
    yyjson_val *root, *sources, *source;
    const char *bindings_name;
    char *bindings_path = NULL;
    yyjson_arr_iter iterator;
    size_t index = 0u;
    SlophStatus status = SLOPH_STATUS_OK;
    memset(out, 0, sizeof(*out));
    out->module = identity;
    separator = strstr(cursor, "::");
    if (libraries == NULL || separator == NULL ||
        !provider_segment(cursor, (size_t)(separator - cursor)))
        return diagnostic(context, SLOPH_STATUS_INVALID_ARGUMENT,
                          "compiler.provider.identity",
                          "invalid native provider identity", "{}");
    root_length = strlen(libraries) + 1u + (size_t)(separator - cursor) +
                  sizeof("/src/") - 1u + identity_length + 1u;
    out->root = malloc(root_length);
    if (out->root == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    position += (size_t)snprintf(out->root, root_length, "%s/%.*s/src/", libraries,
                                (int)(separator - cursor), cursor);
    cursor = separator + 2u;
    for (;;) {
        separator = strstr(cursor, "::");
        if (!provider_segment(cursor, separator != NULL
                                  ? (size_t)(separator - cursor) : strlen(cursor)))
            goto invalid_identity;
        while (*cursor != '\0' && !(cursor[0] == ':' && cursor[1] == ':'))
            out->root[position++] = *cursor++;
        if (*cursor == '\0') break;
        out->root[position++] = '/'; cursor += 2u;
    }
    out->root[position] = '\0';
    manifest = malloc(position + sizeof("/provider.json"));
    if (manifest == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
    (void)snprintf(manifest, position + sizeof("/provider.json"),
                   "%s/provider.json", out->root);
    if (!regular_not_symlink(manifest) || (stream = fopen(manifest, "rb")) == NULL ||
        fseek(stream, 0, SEEK_END) != 0 || (file_size = ftell(stream)) < 0 ||
        file_size > 65536L || fseek(stream, 0, SEEK_SET) != 0) {
        status = diagnostic(context, SLOPH_STATUS_IO_ERROR,
                            "compiler.provider.metadata",
                            "native provider metadata could not be loaded", "{}");
        goto done;
    }
    data = malloc((size_t)file_size + 1u);
    if (data == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
    if (fread(data, 1u, (size_t)file_size, stream) != (size_t)file_size) {
        status = diagnostic(context, SLOPH_STATUS_IO_ERROR,
                            "compiler.provider.metadata",
                            "native provider metadata could not be loaded", "{}");
        goto done;
    }
    data[file_size] = '\0';
    document = yyjson_read(data, (size_t)file_size, 0u);
    root = document != NULL ? yyjson_doc_get_root(document) : NULL;
    sources = root != NULL ? yyjson_obj_get(root, "sources") : NULL;
    if (!exact_provider_object(root) ||
        !yyjson_is_int(yyjson_obj_get(root, "format")) ||
        yyjson_get_sint(yyjson_obj_get(root, "format")) != 1 ||
        !yyjson_is_str(yyjson_obj_get(root, "module")) ||
        strcmp(yyjson_get_str(yyjson_obj_get(root, "module")), identity) != 0 ||
        !yyjson_is_str(yyjson_obj_get(root, "bindings")) ||
        !yyjson_is_arr(sources) || yyjson_arr_size(sources) == 0u) {
        status = diagnostic(context, SLOPH_STATUS_INVALID_ARGUMENT,
                            "compiler.provider.metadata",
                            "native provider metadata is invalid", "{}");
        goto done;
    }
    bindings_name = yyjson_get_str(yyjson_obj_get(root, "bindings"));
    if (!local_file_name(bindings_name)) goto invalid_metadata;
    bindings_path = malloc(position + 1u + strlen(bindings_name) + 1u);
    if (bindings_path == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
    (void)snprintf(bindings_path, position + 1u + strlen(bindings_name) + 1u,
                   "%s/%s", out->root, bindings_name);
    if (!regular_not_symlink(bindings_path)) goto invalid_metadata;
    out->bindings_path = bindings_path;
    bindings_path = NULL;
    out->source_count = yyjson_arr_size(sources);
    out->sources = calloc(out->source_count, sizeof(*out->sources));
    if (out->sources == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
    iterator = yyjson_arr_iter_with(sources);
    while ((source = yyjson_arr_iter_next(&iterator)) != NULL) {
        const char *name;
        size_t length, previous;
        if (!yyjson_is_str(source)) goto invalid_metadata;
        name = yyjson_get_str(source);
        length = strlen(name);
        if (length <= 2u ||
            !provider_segment(name, length - 2u) ||
            (strcmp(name + length - 2u, ".c") != 0 &&
             strcmp(name + length - 2u, ".S") != 0)) goto invalid_metadata;
        for (previous = 0u; previous < index; ++previous)
            if (strcmp(out->sources[previous] + position + 1u, name) == 0)
                goto invalid_metadata;
        out->sources[index] = malloc(position + 1u + length + 1u);
        if (out->sources[index] == NULL) {
            status = SLOPH_STATUS_OUT_OF_MEMORY; goto done;
        }
        (void)snprintf(out->sources[index], position + 1u + length + 1u,
                       "%s/%s", out->root, name);
        if (!regular_not_symlink(out->sources[index])) goto invalid_metadata;
        ++index;
    }
    goto done;
invalid_identity:
    status = diagnostic(context, SLOPH_STATUS_INVALID_ARGUMENT,
                        "compiler.provider.identity",
                        "invalid native provider identity", "{}");
    goto done;
invalid_metadata:
    status = diagnostic(context, SLOPH_STATUS_INVALID_ARGUMENT,
                        "compiler.provider.metadata",
                        "native provider metadata is invalid", "{}");
done:
    if (stream != NULL) fclose(stream);
    yyjson_doc_free(document); free(data); free(manifest); free(bindings_path);
    if (status != SLOPH_STATUS_OK) clear_resolved_provider(out);
    return status;
}

static SlophStatus binding_error(SlophContext *context,
                                 const char *identity) {
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    char *details = NULL;
    SlophStatus status;
    if (document != NULL) {
        yyjson_mut_val *root = yyjson_mut_obj(document);
        yyjson_mut_doc_set_root(document, root);
        yyjson_mut_obj_add_strcpy(document, root, "binding", identity);
        details = yyjson_mut_write(document, 0u, NULL);
        yyjson_mut_doc_free(document);
    }
    status = diagnostic(context, SLOPH_STATUS_INVALID_ARGUMENT,
                        "compiler.provider.binding",
                        "native provider does not declare a required foreign binding",
                        details != NULL ? details : "{}");
    free(details);
    return status;
}

static SlophStatus validate_provider_bindings(
    SlophContext *context, const SlophCoreUnit *unit,
    const ResolvedProvider *provider) {
    FILE *stream = fopen(provider->bindings_path, "rb");
    long file_size;
    char *data = NULL;
    yyjson_doc *document = NULL;
    yyjson_val *root;
    size_t requirement_index;
    SlophStatus status = SLOPH_STATUS_OK;
    if (stream == NULL || fseek(stream, 0, SEEK_END) != 0 ||
        (file_size = ftell(stream)) < 0 || file_size > 1048576L ||
        fseek(stream, 0, SEEK_SET) != 0) {
        status = diagnostic(context, SLOPH_STATUS_IO_ERROR,
                            "compiler.provider.metadata",
                            "native provider binding metadata could not be loaded", "{}");
        goto done;
    }
    data = malloc((size_t)file_size + 1u);
    if (data == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
    if (fread(data, 1u, (size_t)file_size, stream) != (size_t)file_size) {
        status = diagnostic(context, SLOPH_STATUS_IO_ERROR,
                            "compiler.provider.metadata",
                            "native provider binding metadata could not be loaded", "{}");
        goto done;
    }
    document = yyjson_read(data, (size_t)file_size, 0u);
    root = document != NULL ? yyjson_doc_get_root(document) : NULL;
    if (!yyjson_is_arr(root)) {
        status = diagnostic(context, SLOPH_STATUS_INVALID_ARGUMENT,
                            "compiler.provider.metadata",
                            "native provider binding metadata is invalid", "{}");
        goto done;
    }
    for (requirement_index = 0u;
         requirement_index < sloph_heartwood_foreign_requirement_count(unit);
         ++requirement_index) {
        SlophForeignRequirementView requirement;
        yyjson_arr_iter iterator;
        yyjson_val *binding;
        int found = 0;
        if (sloph_heartwood_foreign_requirement(
                unit, requirement_index, &requirement) != SLOPH_STATUS_OK)
            continue;
        if (strcmp(requirement.provider, provider->module) != 0) continue;
        iterator = yyjson_arr_iter_with(root);
        while ((binding = yyjson_arr_iter_next(&iterator)) != NULL) {
            yyjson_val *identity = yyjson_is_obj(binding)
                ? yyjson_obj_get(binding, "identity") : NULL;
            yyjson_val *declared_provider = yyjson_is_obj(binding)
                ? yyjson_obj_get(binding, "provider") : NULL;
            yyjson_val *symbol = yyjson_is_obj(binding)
                ? yyjson_obj_get(binding, "symbol") : NULL;
            yyjson_val *header = yyjson_is_obj(binding)
                ? yyjson_obj_get(binding, "header") : NULL;
            if (yyjson_is_str(identity) && yyjson_is_str(declared_provider) &&
                yyjson_is_str(symbol) && yyjson_is_str(header) &&
                strcmp(yyjson_get_str(identity), requirement.identity) == 0 &&
                strcmp(yyjson_get_str(declared_provider), requirement.provider) == 0 &&
                strcmp(yyjson_get_str(symbol), requirement.symbol) == 0 &&
                strcmp(yyjson_get_str(header), requirement.header) == 0) {
                found = 1; break;
            }
        }
        if (!found) { status = binding_error(context, requirement.identity); break; }
    }
done:
    if (stream != NULL) fclose(stream);
    yyjson_doc_free(document); free(data);
    return status;
}

static char *resolve_compiler(const char *name) {
    const char *path;
    if (name == NULL || *name == '\0') return NULL;
    if (strchr(name, '/') != NULL)
        return executable_file(name) ? copy_text(name) : NULL;
    path = getenv("PATH");
    if (path == NULL) path = "/usr/bin:/bin";
    while (*path != '\0') {
        const char *end = strchr(path, ':');
        size_t directory_length = end != NULL ? (size_t)(end - path) : strlen(path);
        size_t size = (directory_length != 0u ? directory_length : 1u) +
                      1u + strlen(name) + 1u;
        char *candidate = malloc(size);
        if (candidate == NULL) return NULL;
        if (directory_length != 0u) memcpy(candidate, path, directory_length);
        else { candidate[0] = '.'; directory_length = 1u; }
        candidate[directory_length] = '/';
        strcpy(candidate + directory_length + 1u, name);
        if (executable_file(candidate)) return candidate;
        free(candidate);
        if (end == NULL) break;
        path = end + 1u;
    }
    return NULL;
}

static SlophStatus mkdir_parents(SlophContext *context, const char *path) {
    char *copy = copy_text(path);
    char *cursor;
    if (copy == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    for (cursor = copy + 1u; *cursor != '\0'; ++cursor) {
        if (*cursor != '/') continue;
        *cursor = '\0';
        if (mkdir(copy, 0777) != 0 && errno != EEXIST) {
            free(copy);
            return diagnostic(context, SLOPH_STATUS_IO_ERROR, "compiler.output.path",
                              "compiler output directory could not be created", "{}");
        }
        *cursor = '/';
    }
    free(copy);
    return SLOPH_STATUS_OK;
}

static SlophStatus copy_executable(SlophContext *context, const char *source,
                                   const char *destination) {
    char *temporary;
    size_t size = strlen(destination) + sizeof(".tmp.XXXXXX");
    int input = -1, output = -1;
    unsigned char buffer[16384];
    SlophStatus status = mkdir_parents(context, destination);
    if (status != SLOPH_STATUS_OK) return status;
    temporary = malloc(size);
    if (temporary == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    (void)snprintf(temporary, size, "%s.tmp.XXXXXX", destination);
    input = open(source, O_RDONLY);
    output = mkstemp(temporary);
    if (input < 0 || output < 0) status = SLOPH_STATUS_IO_ERROR;
    while (status == SLOPH_STATUS_OK) {
        ssize_t count = read(input, buffer, sizeof(buffer));
        size_t offset = 0u;
        if (count < 0 && errno == EINTR) continue;
        if (count < 0) { status = SLOPH_STATUS_IO_ERROR; break; }
        if (count == 0) break;
        while (offset < (size_t)count) {
            ssize_t wrote = write(output, buffer + offset, (size_t)count - offset);
            if (wrote < 0 && errno == EINTR) continue;
            if (wrote <= 0) { status = SLOPH_STATUS_IO_ERROR; break; }
            offset += (size_t)wrote;
        }
    }
    if (status == SLOPH_STATUS_OK && fchmod(output, 0755) != 0)
        status = SLOPH_STATUS_IO_ERROR;
    if (status == SLOPH_STATUS_OK && fsync(output) != 0)
        status = SLOPH_STATUS_IO_ERROR;
    if (input >= 0 && close(input) != 0 && status == SLOPH_STATUS_OK)
        status = SLOPH_STATUS_IO_ERROR;
    if (output >= 0 && close(output) != 0 && status == SLOPH_STATUS_OK)
        status = SLOPH_STATUS_IO_ERROR;
    if (status == SLOPH_STATUS_OK && rename(temporary, destination) != 0)
        status = SLOPH_STATUS_IO_ERROR;
    if (status != SLOPH_STATUS_OK) {
        (void)unlink(temporary);
        status = diagnostic(context, status, "compiler.output.write",
                            "compiled program could not be written", "{}");
    }
    free(temporary);
    return status;
}

static char *failure_details(const char *compiler, int exit_code,
                             const unsigned char *error, size_t error_length) {
    yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
    yyjson_mut_val *root;
    yyjson_mut_val *value;
    char *text;
    if (document == NULL) return NULL;
    root = yyjson_mut_obj(document);
    yyjson_mut_doc_set_root(document, root);
    yyjson_mut_obj_add_strcpy(document, root, "compiler", compiler);
    yyjson_mut_obj_add_int(document, root, "exit_code", exit_code);
    value = yyjson_mut_strncpy(document,
                               error != NULL ? (const char *)error : "",
                               error_length);
    yyjson_mut_obj_add_val(document, root, "stderr", value);
    text = yyjson_mut_write(document, 0u, NULL);
    yyjson_mut_doc_free(document);
    return text;
}

SlophNativeOptions sloph_native_options_default(void) {
    SlophNativeOptions options;
    options.compiler = "cc";
    options.output_path = NULL;
    options.emit_c_path = NULL;
    options.timings = NULL;
    return options;
}

SlophStatus sloph_native_compile(SlophContext *context,
                                 SlophCoreUnit *heartwood,
                                 const SlophProject *project,
                                 const char *symbol,
                                 const SlophNativeOptions *requested) {
    SlophNativeOptions options = requested != NULL
        ? *requested : sloph_native_options_default();
    char *compiler = NULL;
    char *timber = NULL;
    size_t timber_length = 0u;
    char *directory = NULL;
    char *source_path = NULL;
    char *binary_path = NULL;
    FILE *source = NULL;
    const char **arguments = NULL;
    size_t argument_count = 0u, argument_capacity = 16u, index, item;
    SlophProcessOptions process_options;
    static const SlophProcessEnvironment reproducible_environment[] = {
        {"RC_UUID_SALT", "sloph"},
        {"SOURCE_DATE_EPOCH", "1"},
        {"ZERO_AR_DATE", "1"}
    };
    SlophProcessResult process_result;
    SlophStatus status;
    int temporary_created = 0;
    struct timespec clock_value;
    uint64_t started = 0u, emitted = 0u;
    ResolvedProvider *resolved = NULL;
    size_t resolved_count = 0u;
    char **owned_paths = NULL;
    size_t owned_path_count = 0u, owned_path_capacity = 0u;
#define ADD_ARGUMENT(value) do { \
    if (argument_count == argument_capacity) { \
        size_t next = argument_capacity * 2u; \
        const char **grown = realloc(arguments, next * sizeof(*arguments)); \
        if (grown == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; } \
        arguments = grown; argument_capacity = next; \
    } \
    arguments[argument_count++] = (value); \
} while (0)
#define ADD_PATH(value) do { \
    char *absolute_path = realpath((value), NULL); \
    if (absolute_path == NULL) { \
        status = diagnostic(context, SLOPH_STATUS_IO_ERROR, \
                            "compiler.provider.source", \
                            "native provider path could not be resolved", "{}"); \
        goto done; \
    } \
    if (owned_path_count == owned_path_capacity) { \
        size_t next_capacity = owned_path_capacity == 0u ? 8u : \
                               owned_path_capacity * 2u; \
        char **grown_paths = realloc(owned_paths, \
                                     next_capacity * sizeof(*owned_paths)); \
        if (grown_paths == NULL) { \
            free(absolute_path); status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; \
        } \
        owned_paths = grown_paths; owned_path_capacity = next_capacity; \
    } \
    owned_paths[owned_path_count++] = absolute_path; \
    ADD_ARGUMENT(absolute_path); \
} while (0)
    if (context == NULL || heartwood == NULL || symbol == NULL || *symbol == '\0' ||
        options.output_path == NULL || *options.output_path == '\0')
        return SLOPH_STATUS_INVALID_ARGUMENT;
    compiler = resolve_compiler(options.compiler);
    if (compiler == NULL) {
        char message[1024];
        char *details = NULL;
        yyjson_mut_doc *document = yyjson_mut_doc_new(NULL);
        if (options.compiler == NULL || *options.compiler == '\0') {
            strcpy(message, "C compiler path is empty");
        } else if (strchr(options.compiler, '/') != NULL) {
            (void)snprintf(message, sizeof(message),
                           "C compiler '%s' is not an executable file",
                           options.compiler);
        } else {
            (void)snprintf(message, sizeof(message),
                           "C compiler '%s' was not found on PATH",
                           options.compiler);
        }
        if (document != NULL) {
            yyjson_mut_val *root = yyjson_mut_obj(document);
            yyjson_mut_doc_set_root(document, root);
            if (options.compiler != NULL && *options.compiler != '\0')
                yyjson_mut_obj_add_strcpy(document, root, "compiler",
                                           options.compiler);
            details = yyjson_mut_write(document, 0u, NULL);
            yyjson_mut_doc_free(document);
        }
        status = diagnostic(context, SLOPH_STATUS_PROCESS_ERROR,
                            "compiler.c11.path", message,
                            details != NULL ? details : "{}");
        free(details);
        return status;
    }
    if (options.timings != NULL) {
        memset(options.timings, 0, sizeof(*options.timings));
        if (clock_gettime(CLOCK_MONOTONIC, &clock_value) == 0)
            started = (uint64_t)clock_value.tv_sec * UINT64_C(1000000000) +
                      (uint64_t)clock_value.tv_nsec;
    }
    status = sloph_heartwood_to_timber(context, heartwood, symbol,
                                       &timber, &timber_length);
    if (status != SLOPH_STATUS_OK) goto done;
    if (options.timings != NULL && started != 0u &&
        clock_gettime(CLOCK_MONOTONIC, &clock_value) == 0) {
        emitted = (uint64_t)clock_value.tv_sec * UINT64_C(1000000000) +
                  (uint64_t)clock_value.tv_nsec;
        options.timings->core_to_c_nanoseconds = emitted - started;
    }
    if (project == NULL) {
        size_t requirement_count =
            sloph_heartwood_foreign_requirement_count(heartwood);
        if (requirement_count != 0u) {
            resolved = calloc(requirement_count, sizeof(*resolved));
            if (resolved == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
        }
        for (index = 0u; index < requirement_count; ++index) {
            SlophForeignRequirementView requirement;
            size_t previous;
            status = sloph_heartwood_foreign_requirement(
                heartwood, index, &requirement);
            if (status != SLOPH_STATUS_OK) goto done;
            for (previous = 0u; previous < index; ++previous) {
                SlophForeignRequirementView earlier;
                if (sloph_heartwood_foreign_requirement(
                        heartwood, previous, &earlier) == SLOPH_STATUS_OK &&
                    strcmp(earlier.provider, requirement.provider) == 0)
                    break;
            }
            if (previous != index) continue;
            status = resolve_provider(context, requirement.provider,
                                      &resolved[resolved_count]);
            if (status != SLOPH_STATUS_OK) goto done;
            ++resolved_count;
            status = validate_provider_bindings(context, heartwood,
                                                &resolved[resolved_count - 1u]);
            if (status != SLOPH_STATUS_OK) goto done;
        }
    }
    if (options.emit_c_path != NULL) {
        SlophHost host = sloph_posix_host();
        SlophBytes bytes = {(const unsigned char *)timber, timber_length};
        status = mkdir_parents(context, options.emit_c_path);
        if (status == SLOPH_STATUS_OK)
            status = host.write_file_atomic(host.user_data, context,
                                            options.emit_c_path, bytes, 0666u);
        if (status != SLOPH_STATUS_OK) goto done;
    }
    directory = temporary_template("sloph-c11-");
    if (directory == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
    if (mkdtemp(directory) == NULL) {
        status = diagnostic(context, SLOPH_STATUS_IO_ERROR,
                            "compiler.temporary", "temporary directory could not be created", "{}");
        goto done;
    }
    temporary_created = 1;
    source_path = malloc(strlen(directory) + sizeof("/program.c"));
    binary_path = malloc(strlen(directory) + sizeof("/program"));
    if (source_path == NULL || binary_path == NULL) {
        status = SLOPH_STATUS_OUT_OF_MEMORY; goto done;
    }
    (void)sprintf(source_path, "%s/program.c", directory);
    (void)sprintf(binary_path, "%s/program", directory);
    source = fopen(source_path, "wb");
    if (source == NULL ||
        (timber_length != 0u && fwrite(timber, 1u, timber_length, source) != timber_length)) {
        status = diagnostic(context, SLOPH_STATUS_IO_ERROR,
                            "compiler.temporary", "generated C could not be written", "{}");
        goto done;
    }
    if (fclose(source) != 0) {
        source = NULL;
        status = diagnostic(context, SLOPH_STATUS_IO_ERROR,
                            "compiler.temporary", "generated C could not be written", "{}");
        goto done;
    }
    source = NULL;
    arguments = malloc(argument_capacity * sizeof(*arguments));
    if (arguments == NULL) { status = SLOPH_STATUS_OUT_OF_MEMORY; goto done; }
    ADD_ARGUMENT(compiler); ADD_ARGUMENT("-std=c11"); ADD_ARGUMENT("-O0");
    ADD_ARGUMENT("-g0"); ADD_ARGUMENT("-Wall"); ADD_ARGUMENT("-Wextra");
    ADD_ARGUMENT("-Werror");
#if defined(__linux__) && (defined(__x86_64__) || defined(__amd64__))
    ADD_ARGUMENT("-Wl,--build-id=none");
#endif
    if (project != NULL) {
        for (index = 0u; index < sloph_project_provider_count(project); ++index) {
            SlophProviderSourceView provider;
            if (sloph_project_provider(project, index, &provider) != SLOPH_STATUS_OK)
                continue;
            ADD_ARGUMENT("-I"); ADD_PATH(provider.root);
        }
    } else {
        for (index = 0u; index < resolved_count; ++index) {
            ADD_ARGUMENT("-I"); ADD_PATH(resolved[index].root);
        }
    }
    /* Keep compiler-visible temporary paths deterministic. Darwin's linker
     * derives LC_UUID from the output spelling, so absolute mkdtemp paths
     * would otherwise make byte-identical inputs produce different binaries. */
    ADD_ARGUMENT("program.c");
    if (project != NULL) {
        for (index = 0u; index < sloph_project_provider_count(project); ++index) {
            SlophProviderSourceView provider;
            if (sloph_project_provider(project, index, &provider) != SLOPH_STATUS_OK)
                continue;
            for (item = 0u; item < provider.source_count; ++item)
                ADD_PATH(provider.sources[item]);
        }
    } else {
        for (index = 0u; index < resolved_count; ++index)
            for (item = 0u; item < resolved[index].source_count; ++item)
                ADD_PATH(resolved[index].sources[item]);
    }
    ADD_ARGUMENT("-o"); ADD_ARGUMENT("program"); ADD_ARGUMENT(NULL);
    memset(&process_options, 0, sizeof(process_options));
    process_options.arguments = arguments;
    process_options.working_directory = directory;
    process_options.timeout_seconds = 120u;
    process_options.output_limit = 1048576u;
    process_options.tail_limit = 65536u;
    process_options.environment = reproducible_environment;
    process_options.environment_count = sizeof(reproducible_environment) /
                                        sizeof(reproducible_environment[0]);
    status = sloph_process_run(context, &process_options, &process_result);
    if (status != SLOPH_STATUS_OK) goto done;
    if (process_result.timed_out) {
        status = diagnostic(context, SLOPH_STATUS_PROCESS_ERROR,
                            "compiler.c11.timeout", "C compiler exceeded the 120 second limit", "{}");
    } else if (process_result.pipe_timed_out) {
        status = diagnostic(context, SLOPH_STATUS_PROCESS_ERROR,
                            "compiler.c11.pipe_timeout",
                            "C compiler diagnostic pipes did not close", "{}");
    } else if (process_result.output_exceeded) {
        status = diagnostic(context, SLOPH_STATUS_LIMIT_EXCEEDED,
                            "compiler.c11.output_limit", "C compiler output exceeded 1048576 bytes", "{\"configured\":1048576}");
    } else if (process_result.exit_code != 0) {
        char *details = failure_details(compiler, process_result.exit_code,
                                        process_result.standard_error,
                                        process_result.standard_error_length);
        status = diagnostic(context, SLOPH_STATUS_PROCESS_ERROR,
                            "compiler.c11.failed", "C compiler failed",
                            details != NULL ? details : "{}");
        free(details);
    } else {
        status = copy_executable(context, binary_path, options.output_path);
    }
    sloph_process_result_free(&process_result);
    if (options.timings != NULL && emitted != 0u &&
        clock_gettime(CLOCK_MONOTONIC, &clock_value) == 0) {
        uint64_t ended = (uint64_t)clock_value.tv_sec * UINT64_C(1000000000) +
                         (uint64_t)clock_value.tv_nsec;
        options.timings->c_compile_link_nanoseconds = ended - emitted;
    }
done:
    if (source != NULL) fclose(source);
    if (temporary_created) {
        if (source_path != NULL) (void)unlink(source_path);
        if (binary_path != NULL) (void)unlink(binary_path);
        (void)rmdir(directory);
    }
    free_resolved_providers(resolved, resolved_count);
    free(source_path); free(binary_path); free(directory);
    for (index = 0u; index < owned_path_count; ++index) free(owned_paths[index]);
    free(owned_paths);
    free(arguments); free(timber); free(compiler);
    return status;
#undef ADD_PATH
#undef ADD_ARGUMENT
}
