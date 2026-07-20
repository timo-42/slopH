#include "yyjson.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define INPUT_LIMIT (16u * 1024u * 1024u)

static int fail(const char *code, const char *message) {
    (void)fprintf(stderr, "sloph-jsonfmt: %s: %s\n", code, message);
    return 1;
}

int main(int argc, char **argv) {
    char *input = NULL, *formatted = NULL;
    size_t length = 0u, capacity = 0u, output_length = 0u;
    yyjson_doc *document;
    yyjson_read_err read_error;
    yyjson_write_err write_error;
    (void)argv;
    if (argc != 1) return fail("usage", "input is accepted only on standard input");
    for (;;) {
        size_t count;
        if (capacity - length < 8192u) {
            size_t next = capacity == 0u ? 8192u : capacity * 2u;
            char *grown;
            if (next > INPUT_LIMIT + 1u) next = INPUT_LIMIT + 1u;
            if (next <= capacity) {
                free(input); return fail("input_limit", "input exceeds 16777216 bytes");
            }
            grown = (char *)realloc(input, next);
            if (grown == NULL) { free(input); return fail("memory", "allocation failed"); }
            input = grown; capacity = next;
        }
        count = fread(input + length, 1u, capacity - length, stdin);
        length += count;
        if (length > INPUT_LIMIT) {
            free(input); return fail("input_limit", "input exceeds 16777216 bytes");
        }
        if (count == 0u) {
            if (ferror(stdin)) { free(input); return fail("read", "standard input could not be read"); }
            break;
        }
    }
    document = yyjson_read_opts(input, length, YYJSON_READ_NUMBER_AS_RAW,
                                NULL, &read_error);
    free(input);
    if (document == NULL) {
        char message[192];
        (void)snprintf(message, sizeof(message), "invalid JSON at byte %zu: %s",
                       read_error.pos, read_error.msg != NULL ? read_error.msg : "syntax error");
        return fail("syntax", message);
    }
    formatted = yyjson_write_opts(document, YYJSON_WRITE_PRETTY_TWO_SPACES,
                                  NULL, &output_length, &write_error);
    yyjson_doc_free(document);
    if (formatted == NULL)
        return fail("format", write_error.msg != NULL ? write_error.msg : "formatting failed");
    if ((output_length != 0u && fwrite(formatted, 1u, output_length, stdout) != output_length) ||
        fputc('\n', stdout) == EOF || fflush(stdout) != 0) {
        free(formatted); return fail("write", "standard output could not be written");
    }
    free(formatted);
    return 0;
}
