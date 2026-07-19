#include "sloph/context.h"
#include "sloph/core.h"
#include "sloph/compiler.h"
#include "sloph/host.h"
#include "sloph/project.h"
#include "sloph/syntax.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

typedef enum DiagnosticMode {
    DIAGNOSTIC_HUMAN,
    DIAGNOSTIC_JSONL
} DiagnosticMode;

typedef enum CommandAction {
    ACTION_CORE_CHECK,
    ACTION_CORE_PRINT,
    ACTION_CORE_EVAL,
    ACTION_PROJECT_CHECK,
    ACTION_SOURCE_FORMAT,
    ACTION_AST_CHECK,
    ACTION_AST_PRINT
} CommandAction;

typedef enum InputFormat { INPUT_TEXT, INPUT_SOURCE, INPUT_JSON } InputFormat;
typedef enum FormatMode { FORMAT_STDOUT, FORMAT_CHECK, FORMAT_WRITE } FormatMode;

typedef struct Command {
    DiagnosticMode diagnostics;
    CommandAction action;
    InputFormat input_format;
    FormatMode format_mode;
    int format_mode_set;
    unsigned source_version;
    const char *input;
    const char *output;
    const char *symbol;
    size_t fuel;
    int has_fuel;
    int transformation;
} Command;

static const char TOP_LEVEL_HELP[] =
    "usage: sloph [-h] [--diagnostics {human,jsonl}] [--version]\n"
    "             {unstable,canopy-to-crown,crown-to-heartwood,check,format,ast,core,compile,run} ...\n"
    "\n"
    "positional arguments:\n"
    "  {unstable,canopy-to-crown,crown-to-heartwood,check,format,ast,core,compile,run}\n"
    "    unstable            unstable implementation tools\n"
    "    canopy-to-crown     transform Source canopy into Crown AST JSON\n"
    "    crown-to-heartwood  transform a Crown project into canonical Heartwood Core\n"
    "    check               check a Source v1 project\n"
    "    format              format a Source v1 file\n"
    "    ast                 public Source v1 AST tools\n"
    "    core                public Core v2/v3 tools\n"
    "    compile             compile Source v1 through C11\n"
    "    run                 compile and run a Source v1 project\n"
    "\n"
    "options:\n"
    "  -h, --help            show this help message and exit\n"
    "  --diagnostics {human,jsonl}\n"
    "                        diagnostic rendering (default: human)\n"
    "  --version             show program's version number and exit\n";

static const char CANOPY_TO_CROWN_HELP[] =
    "usage: sloph canopy-to-crown [-h] [-o OUTPUT] INPUT\n"
    "\n"
    "transform Source canopy into deterministic Crown AST JSON\n"
    "\n"
    "positional arguments:\n"
    "  INPUT                 Source file, or - for standard input\n"
    "\n"
    "options:\n"
    "  -h, --help            show this help message and exit\n"
    "  -o OUTPUT, --output OUTPUT\n"
    "                        write Crown AST JSON to OUTPUT (default: -)\n";

static const char CROWN_TO_HEARTWOOD_HELP[] =
    "usage: sloph crown-to-heartwood [-h] [-o OUTPUT] PROJECT\n"
    "\n"
    "transform a Crown project into deterministic canonical Heartwood Core\n"
    "\n"
    "positional arguments:\n"
    "  PROJECT               project directory or sloph.json path\n"
    "\n"
    "options:\n"
    "  -h, --help            show this help message and exit\n"
    "  -o OUTPUT, --output OUTPUT\n"
    "                        write Heartwood Core to OUTPUT (default: -)\n";

static const char *transformation_help(int argc, char **argv) {
    int index = 1;
    if (index < argc && strcmp(argv[index], "--diagnostics") == 0) index += 2;
    else if (index < argc && strncmp(argv[index], "--diagnostics=", 14u) == 0)
        ++index;
    if (index < argc && strcmp(argv[index], "unstable") == 0) ++index;
    if (index + 2 != argc ||
        (strcmp(argv[index + 1], "-h") != 0 &&
         strcmp(argv[index + 1], "--help") != 0)) return NULL;
    if (strcmp(argv[index], "canopy-to-crown") == 0)
        return CANOPY_TO_CROWN_HELP;
    if (strcmp(argv[index], "crown-to-heartwood") == 0)
        return CROWN_TO_HEARTWOOD_HELP;
    return NULL;
}

static int common_flag(int argc, char **argv, const char *short_name,
                       const char *long_name) {
    int index = 1;
    if (index < argc && strcmp(argv[index], "--diagnostics") == 0) index += 2;
    else if (index < argc && strncmp(argv[index], "--diagnostics=", 14u) == 0)
        ++index;
    return index + 1 == argc &&
           (strcmp(argv[index], long_name) == 0 ||
            (short_name != NULL && strcmp(argv[index], short_name) == 0));
}

static void json_string(FILE *stream, const char *text) {
    const unsigned char *cursor = (const unsigned char *)text;
    fputc('"', stream);
    while (*cursor != 0u) {
        unsigned char byte = *cursor++;
        switch (byte) {
        case '"': fputs("\\\"", stream); break;
        case '\\': fputs("\\\\", stream); break;
        case '\b': fputs("\\b", stream); break;
        case '\f': fputs("\\f", stream); break;
        case '\n': fputs("\\n", stream); break;
        case '\r': fputs("\\r", stream); break;
        case '\t': fputs("\\t", stream); break;
        default:
            if (byte < 0x20u) (void)fprintf(stream, "\\u%04x", byte);
            else fputc((int)byte, stream);
            break;
        }
    }
    fputc('"', stream);
}

static char *input_details(const char *path) {
    const unsigned char *source = (const unsigned char *)path;
    size_t length = strlen(path);
    size_t capacity;
    size_t position = 0u;
    char *details;
    if (length > (SIZE_MAX - 14u) / 6u) return NULL;
    capacity = length * 6u + 14u;
    details = (char *)malloc(capacity);
    if (details == NULL) return NULL;
    memcpy(details, "{\"input\":\"", 10u);
    position = 10u;
    while (*source != 0u) {
        unsigned char byte = *source++;
        if (byte == '"' || byte == '\\') {
            details[position++] = '\\'; details[position++] = (char)byte;
        } else if (byte == '\b') {
            memcpy(details + position, "\\b", 2u); position += 2u;
        } else if (byte == '\f') {
            memcpy(details + position, "\\f", 2u); position += 2u;
        } else if (byte == '\n') {
            memcpy(details + position, "\\n", 2u); position += 2u;
        } else if (byte == '\r') {
            memcpy(details + position, "\\r", 2u); position += 2u;
        } else if (byte == '\t') {
            memcpy(details + position, "\\t", 2u); position += 2u;
        } else if (byte < 0x20u) {
            (void)snprintf(details + position, 7u, "\\u%04x", byte);
            position += 6u;
        } else {
            details[position++] = (char)byte;
        }
    }
    details[position++] = '"'; details[position++] = '}'; details[position] = '\0';
    return details;
}

static SlophStatus add_io_diagnostic(SlophContext *context,
                                     const char *input_path,
                                     const char *failed_path,
                                     int error_number) {
    char message[1024];
    char *details = input_details(input_path);
    (void)snprintf(message, sizeof(message), "[Errno %d] %s: '%s'",
                   error_number, strerror(error_number), failed_path);
    (void)sloph_context_add_diagnostic_full(
        context, "tool.io", "environment", message,
        details != NULL ? details : "{}", SLOPH_UNKNOWN_SPAN,
        SLOPH_SEVERITY_ERROR);
    free(details);
    return SLOPH_STATUS_IO_ERROR;
}

static void render_one_diagnostic(FILE *stream, DiagnosticMode mode,
                                  const char *source,
                                  const SlophDiagnosticView *diagnostic) {
    if (mode == DIAGNOSTIC_HUMAN) {
        (void)fprintf(stream, "%s:%zu:%zu: %s[%s]: %s\n", source,
                      diagnostic->span.start, diagnostic->span.end,
                      sloph_severity_name(diagnostic->severity), diagnostic->code,
                      diagnostic->message);
        return;
    }
    fputs("{\"code\":", stream);
    json_string(stream, diagnostic->code);
    fputs(",\"details\":", stream);
    fputs(diagnostic->details_json != NULL ? diagnostic->details_json : "{}", stream);
    fputs(",\"message\":", stream);
    json_string(stream, diagnostic->message);
    fputs(",\"message_id\":", stream);
    json_string(stream, diagnostic->code);
    fputs(",\"phase\":", stream);
    json_string(stream, diagnostic->phase);
    fputs(",\"schema\":\"sloph.diagnostic\",\"severity\":", stream);
    json_string(stream, sloph_severity_name(diagnostic->severity));
    (void)fprintf(stream, ",\"span\":{\"end\":%zu,\"start\":%zu},\"version\":0}\n",
                  diagnostic->span.end, diagnostic->span.start);
}

static void render_context_diagnostics(SlophContext *context,
                                       DiagnosticMode mode,
                                       const char *source) {
    size_t index;
    for (index = 0u; index < sloph_context_diagnostic_count(context); ++index) {
        SlophDiagnosticView diagnostic;
        if (sloph_context_diagnostic(context, index, &diagnostic) == SLOPH_STATUS_OK)
            render_one_diagnostic(stderr, mode, source, &diagnostic);
    }
}

static void render_standalone_diagnostic(DiagnosticMode mode, const char *source,
                                         const char *code, const char *phase,
                                         const char *message) {
    SlophDiagnosticView diagnostic;
    diagnostic.code = code;
    diagnostic.phase = phase;
    diagnostic.message = message;
    diagnostic.details_json = "{}";
    diagnostic.span = SLOPH_UNKNOWN_SPAN;
    diagnostic.severity = SLOPH_SEVERITY_ERROR;
    render_one_diagnostic(stderr, mode, source, &diagnostic);
}

static int parse_positive_size(const char *text, size_t *out_value) {
    unsigned long long value;
    char *end;
    if (text == NULL || *text == '\0' || *text == '-') return 0;
    errno = 0;
    value = strtoull(text, &end, 10);
    if (errno != 0 || *end != '\0' || value == 0u ||
        value > (unsigned long long)SIZE_MAX) return 0;
    *out_value = (size_t)value;
    return 1;
}

static int take_value(int argc, char **argv, int *index, const char **out_value) {
    if (*index + 1 >= argc) return 0;
    ++*index;
    *out_value = argv[*index];
    return 1;
}

static int parse_command(int argc, char **argv, Command *command,
                         const char **out_error) {
    int index = 1;
    int unstable = 0;
    const char *group;
    memset(command, 0, sizeof(*command));
    command->diagnostics = DIAGNOSTIC_HUMAN;
    command->output = "-";
    if (index < argc && strcmp(argv[index], "--diagnostics") == 0) {
        const char *value;
        if (!take_value(argc, argv, &index, &value)) {
            *out_error = "argument --diagnostics: expected one argument"; return 0;
        }
        if (strcmp(value, "jsonl") == 0) command->diagnostics = DIAGNOSTIC_JSONL;
        else if (strcmp(value, "human") != 0) {
            *out_error = "argument --diagnostics: invalid choice"; return 0;
        }
        ++index;
    } else if (index < argc && strncmp(argv[index], "--diagnostics=", 14u) == 0) {
        const char *value = argv[index] + 14u;
        if (strcmp(value, "jsonl") == 0) command->diagnostics = DIAGNOSTIC_JSONL;
        else if (strcmp(value, "human") != 0) {
            *out_error = "argument --diagnostics: invalid choice"; return 0;
        }
        ++index;
    }
    if (index < argc && strcmp(argv[index], "unstable") == 0) {
        unstable = 1; ++index;
    }
    command->source_version = unstable ? 0u : 1u;
    if (index >= argc) {
        *out_error = "the following arguments are required: command"; return 0;
    }
    group = argv[index++];
    if (strcmp(group, "canopy-to-crown") == 0) {
        command->action = ACTION_AST_PRINT;
        command->input_format = INPUT_SOURCE;
        command->transformation = 1;
    } else if (strcmp(group, "crown-to-heartwood") == 0) {
        command->action = ACTION_CORE_PRINT;
        command->input_format = INPUT_SOURCE;
        command->transformation = 2;
    } else if (strcmp(group, "core") == 0) {
        const char *action;
        if (index >= argc) {
            *out_error = "the following arguments are required: core command"; return 0;
        }
        action = argv[index++];
        if (strcmp(action, "check") == 0) command->action = ACTION_CORE_CHECK;
        else if (strcmp(action, "print") == 0) command->action = ACTION_CORE_PRINT;
        else if (strcmp(action, "eval") == 0 && unstable)
            command->action = ACTION_CORE_EVAL;
        else { *out_error = "invalid Core command"; return 0; }
        command->input_format = (!unstable && command->action == ACTION_CORE_PRINT)
                                    ? INPUT_SOURCE : INPUT_TEXT;
    } else if (strcmp(group, "check") == 0) {
        command->action = ACTION_PROJECT_CHECK;
        command->input_format = INPUT_SOURCE;
    } else if (strcmp(group, "format") == 0) {
        command->action = ACTION_SOURCE_FORMAT;
        command->input_format = INPUT_SOURCE;
        command->format_mode = FORMAT_STDOUT;
    } else if (strcmp(group, "ast") == 0) {
        const char *action;
        if (index >= argc) {
            *out_error = "the following arguments are required: ast command"; return 0;
        }
        action = argv[index++];
        if (strcmp(action, "check") == 0) command->action = ACTION_AST_CHECK;
        else if (strcmp(action, "print") == 0) command->action = ACTION_AST_PRINT;
        else { *out_error = "invalid AST command"; return 0; }
        command->input_format = INPUT_SOURCE;
    } else {
        *out_error = "invalid command"; return 0;
    }
    while (index < argc) {
        const char *option = argv[index];
        const char *value;
        if (strcmp(option, "--symbol") == 0 && command->action == ACTION_CORE_EVAL) {
            if (!take_value(argc, argv, &index, &value)) {
                *out_error = "argument --symbol: expected one argument"; return 0;
            }
            command->symbol = value;
        } else if (strcmp(option, "--fuel") == 0 &&
                   command->action == ACTION_CORE_EVAL) {
            if (!take_value(argc, argv, &index, &value)) {
                *out_error = "argument --fuel: expected one argument"; return 0;
            }
            if (!parse_positive_size(value, &command->fuel)) {
                *out_error = "argument --fuel: must be greater than zero"; return 0;
            }
            command->has_fuel = 1;
        } else if ((strcmp(option, "-o") == 0 ||
                    strcmp(option, "--output") == 0) &&
                   (command->action == ACTION_CORE_PRINT ||
                    command->action == ACTION_AST_PRINT)) {
            if (!take_value(argc, argv, &index, &value)) {
                *out_error = "argument --output: expected one argument"; return 0;
            }
            command->output = value;
        } else if (strcmp(option, "--input-format") == 0 &&
                   command->transformation == 0 &&
                   (command->action == ACTION_CORE_CHECK ||
                    command->action == ACTION_CORE_PRINT ||
                    command->action == ACTION_AST_CHECK ||
                    command->action == ACTION_AST_PRINT)) {
            if (!take_value(argc, argv, &index, &value)) {
                *out_error = "argument --input-format: expected one argument"; return 0;
            }
            if (unstable && command->action == ACTION_CORE_CHECK) {
                *out_error = "unrecognized arguments"; return 0;
            }
            if (command->action == ACTION_AST_CHECK ||
                command->action == ACTION_AST_PRINT) {
                if (strcmp(value, "source") == 0) command->input_format = INPUT_SOURCE;
                else if (strcmp(value, "json") == 0) command->input_format = INPUT_JSON;
                else { *out_error = "argument --input-format: invalid choice"; return 0; }
            } else {
                if (strcmp(value, "text") == 0) command->input_format = INPUT_TEXT;
                else if (strcmp(value, "source") == 0) command->input_format = INPUT_SOURCE;
                else { *out_error = "argument --input-format: invalid choice"; return 0; }
            }
        } else if (strcmp(option, "--format") == 0 &&
                   command->action == ACTION_AST_PRINT &&
                   command->transformation == 0) {
            if (!take_value(argc, argv, &index, &value)) {
                *out_error = "argument --format: expected one argument"; return 0;
            }
            if (strcmp(value, "json") != 0) {
                *out_error = "argument --format: invalid choice"; return 0;
            }
        } else if (strcmp(option, "--stdout") == 0 &&
                   command->action == ACTION_SOURCE_FORMAT) {
            if (command->format_mode_set) {
                if (command->format_mode == FORMAT_STDOUT) { ++index; continue; }
                *out_error = command->format_mode == FORMAT_CHECK
                    ? "argument --stdout: not allowed with argument --check"
                    : "argument --stdout: not allowed with argument --write";
                return 0;
            }
            command->format_mode = FORMAT_STDOUT;
            command->format_mode_set = 1;
        } else if (strcmp(option, "--check") == 0 &&
                   command->action == ACTION_SOURCE_FORMAT) {
            if (command->format_mode_set) {
                if (command->format_mode == FORMAT_CHECK) { ++index; continue; }
                *out_error = command->format_mode == FORMAT_STDOUT
                    ? "argument --check: not allowed with argument --stdout"
                    : "argument --check: not allowed with argument --write";
                return 0;
            }
            command->format_mode = FORMAT_CHECK;
            command->format_mode_set = 1;
        } else if (strcmp(option, "--write") == 0 &&
                   command->action == ACTION_SOURCE_FORMAT) {
            if (command->format_mode_set) {
                if (command->format_mode == FORMAT_WRITE) { ++index; continue; }
                *out_error = command->format_mode == FORMAT_STDOUT
                    ? "argument --write: not allowed with argument --stdout"
                    : "argument --write: not allowed with argument --check";
                return 0;
            }
            command->format_mode = FORMAT_WRITE;
            command->format_mode_set = 1;
        } else if ((option[0] != '-' || strcmp(option, "-") == 0) &&
                   command->input == NULL) {
            command->input = option;
        } else {
            *out_error = "unrecognized arguments"; return 0;
        }
        ++index;
    }
    if (command->input == NULL) {
        *out_error = "the following arguments are required: INPUT"; return 0;
    }
    if (command->action == ACTION_CORE_EVAL && command->symbol == NULL) {
        *out_error = "the following arguments are required: --symbol"; return 0;
    }
    if (command->action == ACTION_SOURCE_FORMAT &&
        command->format_mode == FORMAT_WRITE && strcmp(command->input, "-") == 0) {
        *out_error = "--write requires a file input"; return 0;
    }
    return 1;
}

static SlophStatus read_input(SlophContext *context, const char *path,
                              int syntax_profile,
                              unsigned char **out_data, size_t *out_length) {
    const SlophLimits *limits = sloph_context_limits(context);
    FILE *stream = stdin;
    unsigned char *data;
    size_t length = 0u;
    size_t capacity = limits->input_bytes < 4096u ? limits->input_bytes + 1u : 4096u;
    int saved_errno = 0;
    if (strcmp(path, "-") != 0) {
        stream = fopen(path, "rb");
        if (stream == NULL) {
            saved_errno = errno;
            return add_io_diagnostic(context, path, path, saved_errno);
        }
    }
    data = (unsigned char *)malloc(capacity == 0u ? 1u : capacity);
    if (data == NULL) {
        if (stream != stdin) fclose(stream);
        return SLOPH_STATUS_OUT_OF_MEMORY;
    }
    while (length <= limits->input_bytes) {
        size_t available = capacity - length;
        size_t count;
        if (available == 0u) {
            size_t next = capacity > limits->input_bytes / 2u
                              ? limits->input_bytes + 1u : capacity * 2u;
            unsigned char *grown = (unsigned char *)realloc(data, next);
            if (grown == NULL) {
                free(data); if (stream != stdin) fclose(stream);
                return SLOPH_STATUS_OUT_OF_MEMORY;
            }
            data = grown; capacity = next; available = capacity - length;
        }
        count = fread(data + length, 1u, available, stream);
        length += count;
        if (count < available) {
            if (ferror(stream)) {
                saved_errno = errno;
                free(data); if (stream != stdin) fclose(stream);
                return add_io_diagnostic(context, path, path, saved_errno);
            }
            break;
        }
    }
    if (stream != stdin && fclose(stream) != 0) {
        saved_errno = errno;
        free(data);
        return add_io_diagnostic(context, path, path, saved_errno);
    }
    if (length > limits->input_bytes) {
        char message[160];
        char details[160];
        (void)snprintf(message, sizeof(message),
                       "input_bytes limit exceeded (configured %zu)",
                       limits->input_bytes);
        (void)snprintf(details, sizeof(details),
                       "{\"configured\":%zu,\"limit\":\"input_bytes\"}",
                       limits->input_bytes);
        (void)sloph_context_add_diagnostic_full(
            context, syntax_profile ? "syntax.parse.limit_exceeded" :
                                      "core.parse.limit_exceeded",
            "parse", message, details,
            (SlophSpan){0u, length}, SLOPH_SEVERITY_ERROR);
        free(data); return SLOPH_STATUS_LIMIT_EXCEEDED;
    }
    *out_data = data; *out_length = length;
    return SLOPH_STATUS_OK;
}

static SlophStatus check_project_input(SlophContext *context,
                                       const char *input) {
    struct stat info;
    int error_number;
    char *manifest = NULL;
    size_t length;
    int slash;
    if (stat(input, &info) != 0)
        return add_io_diagnostic(context, input, input, errno);
    if (!S_ISDIR(info.st_mode)) return SLOPH_STATUS_OK;
    length = strlen(input);
    slash = length == 0u || input[length - 1u] != '/';
    if (length > SIZE_MAX - sizeof("sloph.json") - (size_t)slash)
        return SLOPH_STATUS_LIMIT_EXCEEDED;
    manifest = malloc(length + (size_t)slash + sizeof("sloph.json"));
    if (manifest == NULL) return SLOPH_STATUS_OUT_OF_MEMORY;
    memcpy(manifest, input, length);
    if (slash) manifest[length++] = '/';
    memcpy(manifest + length, "sloph.json", sizeof("sloph.json"));
    if (stat(manifest, &info) == 0) {
        free(manifest);
        return SLOPH_STATUS_OK;
    }
    error_number = errno;
    (void)add_io_diagnostic(context, input, manifest, error_number);
    free(manifest);
    return SLOPH_STATUS_IO_ERROR;
}

static const char *libraries_root(void) {
    const char *override = getenv("SLOPH_LIBRARIES_ROOT");
    if (override != NULL && *override != '\0') return override;
#ifdef SLOPH_LIBRARIES_ROOT
    return SLOPH_LIBRARIES_ROOT;
#else
    return NULL;
#endif
}

static SlophStatus load_project(SlophContext *context, const Command *command,
                                SlophProject **out_project) {
    SlophHost host = sloph_posix_host();
    SlophProjectOptions options = sloph_project_options_default();
    options.host = &host;
    options.libraries_root = libraries_root();
    options.source_version = command->source_version;
    return sloph_project_load(context, command->input, &options, out_project);
}

static SlophStatus write_atomic(SlophContext *context, const char *path,
                                const char *text, size_t length,
                                unsigned int mode) {
    SlophHost host = sloph_posix_host();
    SlophBytes bytes;
    bytes.data = (const unsigned char *)text;
    bytes.length = length;
    return host.write_file_atomic(host.user_data, context, path, bytes, mode);
}

static SlophStatus write_output(const char *path, const char *text, size_t length) {
    FILE *stream = stdout;
    if (strcmp(path, "-") != 0) {
        stream = fopen(path, "wb");
        if (stream == NULL) return SLOPH_STATUS_IO_ERROR;
    }
    if (length != 0u && fwrite(text, 1u, length, stream) != length) {
        if (stream != stdout) fclose(stream);
        return SLOPH_STATUS_IO_ERROR;
    }
    if (stream != stdout && fclose(stream) != 0) return SLOPH_STATUS_IO_ERROR;
    return SLOPH_STATUS_OK;
}

static int status_exit_code(SlophStatus status, SlophContext *context) {
    size_t index;
    if (status == SLOPH_STATUS_OK) return 0;
    for (index = 0u; index < sloph_context_diagnostic_count(context); ++index) {
        SlophDiagnosticView diagnostic;
        if (sloph_context_diagnostic(context, index, &diagnostic) == SLOPH_STATUS_OK &&
            strcmp(diagnostic.phase, "environment") == 0) return 3;
    }
    if (status == SLOPH_STATUS_IO_ERROR || status == SLOPH_STATUS_PROCESS_ERROR)
        return 3;
    if (status == SLOPH_STATUS_OUT_OF_MEMORY || status == SLOPH_STATUS_INTERNAL_ERROR)
        return 4;
    return 1;
}

int main(int argc, char **argv) {
    Command command;
    const char *usage_error = NULL;
    SlophContextConfig config;
    SlophContext *context = NULL;
    SlophCoreUnit *unit = NULL;
    SlophProject *project = NULL;
    SlophSyntaxModule *module = NULL;
    SlophSyntaxText syntax_output = {NULL, 0u};
    unsigned char *input = NULL;
    size_t input_length = 0u;
    char *output = NULL;
    size_t output_length = 0u;
    SlophStatus status;
    int exit_code;
    int format_changed = 0;
    const char *command_help = transformation_help(argc, argv);
    if (command_help != NULL) {
        fputs(command_help, stdout);
        return 0;
    }
    if (common_flag(argc, argv, "-h", "--help")) {
        fputs(TOP_LEVEL_HELP, stdout);
        return 0;
    }
    if (common_flag(argc, argv, NULL, "--version")) {
        fputs("sloph 0.0.0\n", stdout);
        return 0;
    }
    if (!parse_command(argc, argv, &command, &usage_error)) {
        DiagnosticMode mode = command.diagnostics;
        render_standalone_diagnostic(mode, "<command-line>", "tool.usage", "cli",
                                     usage_error);
        return 2;
    }
    config = sloph_context_config_default();
    if (command.has_fuel) config.limits.fuel = command.fuel;
    status = sloph_context_create(&config, &context);
    if (status != SLOPH_STATUS_OK) {
        render_standalone_diagnostic(command.diagnostics, command.input,
                                     "tool.internal", "internal",
                                     "internal compiler failure: allocation");
        return 4;
    }
    status = SLOPH_STATUS_OK;
    if (command.action == ACTION_PROJECT_CHECK ||
        ((command.action == ACTION_CORE_CHECK ||
          command.action == ACTION_CORE_PRINT) &&
         command.input_format == INPUT_SOURCE)) {
        status = check_project_input(context, command.input);
        if (status == SLOPH_STATUS_OK)
            status = load_project(context, &command, &project);
        if (status == SLOPH_STATUS_OK)
            status = sloph_project_elaborate(context, project, &unit);
        if (status == SLOPH_STATUS_OK && command.action == ACTION_CORE_PRINT)
            status = sloph_core_print(context, unit, &output, &output_length);
    } else if (command.action == ACTION_CORE_CHECK ||
               command.action == ACTION_CORE_PRINT ||
               command.action == ACTION_CORE_EVAL) {
        status = read_input(context, command.input, 0, &input, &input_length);
        if (status == SLOPH_STATUS_OK)
            status = sloph_core_parse(context, input, input_length, &unit);
        if (status == SLOPH_STATUS_OK) {
            if (command.action == ACTION_CORE_CHECK)
                status = sloph_core_validate(context, unit);
            else if (command.action == ACTION_CORE_PRINT)
                status = sloph_core_print(context, unit, &output, &output_length);
            else
                status = sloph_core_evaluate(context, unit, command.symbol,
                                             &output, &output_length);
        }
    } else {
        status = read_input(context, command.input, 1, &input, &input_length);
        if (status == SLOPH_STATUS_OK) {
            if (command.input_format == INPUT_JSON)
                status = sloph_syntax_from_json(context, input, input_length,
                                                command.source_version, &module);
            else
                status = sloph_syntax_parse(context, input, input_length,
                                            command.source_version, &module);
        }
        if (status == SLOPH_STATUS_OK && command.action == ACTION_AST_CHECK)
            status = sloph_syntax_validate(context, module);
        else if (status == SLOPH_STATUS_OK && command.action == ACTION_AST_PRINT)
            status = sloph_syntax_to_json(context, module, &syntax_output);
        else if (status == SLOPH_STATUS_OK && command.action == ACTION_SOURCE_FORMAT)
            status = sloph_syntax_format(context, module, &syntax_output);
        if (status == SLOPH_STATUS_OK && command.action == ACTION_SOURCE_FORMAT) {
            if (command.format_mode == FORMAT_CHECK) {
                format_changed = input_length != syntax_output.length ||
                    memcmp(input, syntax_output.data, input_length) != 0;
            } else if (command.format_mode == FORMAT_WRITE) {
                unsigned int mode = 0666u;
                struct stat info;
                if (stat(command.input, &info) == 0)
                    mode = (unsigned int)(info.st_mode & 07777u);
                status = write_atomic(context, command.input, syntax_output.data,
                                      syntax_output.length, mode & 0777u);
            }
        }
        if (status == SLOPH_STATUS_OK && syntax_output.data != NULL &&
            (command.action == ACTION_AST_PRINT ||
             (command.action == ACTION_SOURCE_FORMAT &&
              command.format_mode == FORMAT_STDOUT))) {
            status = write_output(command.output, syntax_output.data,
                                  syntax_output.length);
        }
    }
    if (status == SLOPH_STATUS_OK && output != NULL) {
        status = write_output(command.output, output, output_length);
        if (status != SLOPH_STATUS_OK) {
            char *details = input_details(command.input);
            (void)sloph_context_add_diagnostic_full(
                context, "tool.io", "environment", strerror(errno),
                details != NULL ? details : "{}", SLOPH_UNKNOWN_SPAN,
                SLOPH_SEVERITY_ERROR);
            free(details);
        }
    }
    render_context_diagnostics(context, command.diagnostics, command.input);
    exit_code = status_exit_code(status, context);
    if (exit_code == 0 && format_changed) exit_code = 1;
    if (exit_code == 4 && sloph_context_diagnostic_count(context) == 0u)
        render_standalone_diagnostic(command.diagnostics, command.input,
                                     "tool.internal", "internal",
                                     "internal compiler failure: allocation");
    free(output);
    sloph_syntax_text_free(context, &syntax_output);
    sloph_syntax_module_free(module);
    sloph_project_free(project);
    sloph_core_free(unit);
    free(input);
    sloph_context_destroy(context);
    return exit_code;
}
