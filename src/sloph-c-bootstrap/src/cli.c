#include "sloph/context.h"
#include "sloph/core.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef enum DiagnosticMode {
    DIAGNOSTIC_HUMAN,
    DIAGNOSTIC_JSONL
} DiagnosticMode;

typedef enum CoreAction {
    CORE_ACTION_CHECK,
    CORE_ACTION_PRINT,
    CORE_ACTION_EVAL
} CoreAction;

typedef struct Command {
    DiagnosticMode diagnostics;
    CoreAction action;
    const char *input;
    const char *output;
    const char *symbol;
    size_t fuel;
    int has_fuel;
} Command;

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
    const char *action;
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
    (void)unstable;
    if (index >= argc || strcmp(argv[index], "core") != 0) {
        *out_error = "the following arguments are required: command"; return 0;
    }
    ++index;
    if (index >= argc) {
        *out_error = "the following arguments are required: core command"; return 0;
    }
    action = argv[index++];
    if (strcmp(action, "check") == 0) command->action = CORE_ACTION_CHECK;
    else if (strcmp(action, "print") == 0) command->action = CORE_ACTION_PRINT;
    else if (strcmp(action, "eval") == 0 && unstable) command->action = CORE_ACTION_EVAL;
    else {
        *out_error = "invalid Core command"; return 0;
    }
    if (index >= argc) {
        *out_error = "the following arguments are required: INPUT"; return 0;
    }
    command->input = argv[index++];
    while (index < argc) {
        const char *option = argv[index];
        const char *value;
        if (strcmp(option, "--symbol") == 0 && command->action == CORE_ACTION_EVAL) {
            if (!take_value(argc, argv, &index, &value)) {
                *out_error = "argument --symbol: expected one argument"; return 0;
            }
            command->symbol = value;
        } else if (strcmp(option, "--fuel") == 0 &&
                   command->action == CORE_ACTION_EVAL) {
            if (!take_value(argc, argv, &index, &value)) {
                *out_error = "argument --fuel: expected one argument"; return 0;
            }
            if (!parse_positive_size(value, &command->fuel)) {
                *out_error = "argument --fuel: must be greater than zero"; return 0;
            }
            command->has_fuel = 1;
        } else if ((strcmp(option, "-o") == 0 ||
                    strcmp(option, "--output") == 0) &&
                   command->action == CORE_ACTION_PRINT) {
            if (!take_value(argc, argv, &index, &value)) {
                *out_error = "argument --output: expected one argument"; return 0;
            }
            command->output = value;
        } else if (strcmp(option, "--input-format") == 0 &&
                   command->action != CORE_ACTION_EVAL) {
            if (!take_value(argc, argv, &index, &value)) {
                *out_error = "argument --input-format: expected one argument"; return 0;
            }
            if (strcmp(value, "text") != 0) {
                *out_error = "C bootstrap currently supports Core text input only";
                return 0;
            }
        } else {
            *out_error = "unrecognized arguments"; return 0;
        }
        ++index;
    }
    if (command->action == CORE_ACTION_EVAL && command->symbol == NULL) {
        *out_error = "the following arguments are required: --symbol"; return 0;
    }
    return 1;
}

static SlophStatus read_input(SlophContext *context, const char *path,
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
            char message[512];
            char *details;
            saved_errno = errno;
            (void)snprintf(message, sizeof(message), "%s", strerror(saved_errno));
            details = input_details(path);
            (void)sloph_context_add_diagnostic_full(
                context, "tool.io", "environment", message,
                details != NULL ? details : "{}",
                SLOPH_UNKNOWN_SPAN, SLOPH_SEVERITY_ERROR);
            free(details);
            return SLOPH_STATUS_IO_ERROR;
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
                char message[512];
                char *details;
                saved_errno = errno;
                (void)snprintf(message, sizeof(message), "%s", strerror(saved_errno));
                details = input_details(path);
                (void)sloph_context_add_diagnostic_full(
                    context, "tool.io", "environment", message,
                    details != NULL ? details : "{}",
                    SLOPH_UNKNOWN_SPAN, SLOPH_SEVERITY_ERROR);
                free(details);
                free(data); if (stream != stdin) fclose(stream);
                return SLOPH_STATUS_IO_ERROR;
            }
            break;
        }
    }
    if (stream != stdin && fclose(stream) != 0) {
        char message[512];
        char *details;
        (void)snprintf(message, sizeof(message), "%s", strerror(errno));
        details = input_details(path);
        (void)sloph_context_add_diagnostic_full(
            context, "tool.io", "environment", message,
            details != NULL ? details : "{}",
            SLOPH_UNKNOWN_SPAN, SLOPH_SEVERITY_ERROR);
        free(details);
        free(data); return SLOPH_STATUS_IO_ERROR;
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
            context, "core.parse.limit_exceeded", "parse", message, details,
            (SlophSpan){0u, length}, SLOPH_SEVERITY_ERROR);
        free(data); return SLOPH_STATUS_LIMIT_EXCEEDED;
    }
    *out_data = data; *out_length = length;
    return SLOPH_STATUS_OK;
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
    unsigned char *input = NULL;
    size_t input_length = 0u;
    char *output = NULL;
    size_t output_length = 0u;
    SlophStatus status;
    int exit_code;
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
    status = read_input(context, command.input, &input, &input_length);
    if (status == SLOPH_STATUS_OK)
        status = sloph_core_parse(context, input, input_length, &unit);
    if (status == SLOPH_STATUS_OK) {
        if (command.action == CORE_ACTION_CHECK)
            status = sloph_core_validate(context, unit);
        else if (command.action == CORE_ACTION_PRINT)
            status = sloph_core_print(context, unit, &output, &output_length);
        else
            status = sloph_core_evaluate(context, unit, command.symbol,
                                         &output, &output_length);
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
    if (exit_code == 4 && sloph_context_diagnostic_count(context) == 0u)
        render_standalone_diagnostic(command.diagnostics, command.input,
                                     "tool.internal", "internal",
                                     "internal compiler failure: allocation");
    free(output);
    sloph_core_free(unit);
    free(input);
    sloph_context_destroy(context);
    return exit_code;
}
