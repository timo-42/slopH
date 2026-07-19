#include "sloph/context.h"
#include "sloph/syntax.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv) {
    FILE *file; long length; unsigned char *source; SlophContext *context = NULL;
    SlophSyntaxModule *module = NULL; SlophSyntaxText text = {0}; SlophStatus status;
    unsigned version;
    if (argc != 4) return 64;
    version = (unsigned)strtoul(argv[1], NULL, 10);
    file = fopen(argv[3], "rb"); if (file == NULL) return 66;
    if (fseek(file, 0, SEEK_END) != 0 || (length = ftell(file)) < 0 ||
        fseek(file, 0, SEEK_SET) != 0) return 74;
    source = malloc((size_t)length); if (source == NULL) return 70;
    if (fread(source, 1u, (size_t)length, file) != (size_t)length) return 74;
    (void)fclose(file);
    if (sloph_context_create(NULL, &context) != SLOPH_STATUS_OK) return 70;
    if (strcmp(argv[2], "roundtrip") == 0)
        status = sloph_syntax_from_json(context, source, (size_t)length, version,
                                        &module);
    else
        status = sloph_syntax_parse(context, source, (size_t)length, version, &module);
    free(source);
    if (status != SLOPH_STATUS_OK) return 2;
    status = (strcmp(argv[2], "json") == 0 || strcmp(argv[2], "roundtrip") == 0) ? sloph_syntax_to_json(context, module, &text)
                                           : sloph_syntax_format(context, module, &text);
    if (status == SLOPH_STATUS_OK) (void)fwrite(text.data, 1u, text.length, stdout);
    sloph_syntax_text_free(context, &text); sloph_syntax_module_free(module);
    sloph_context_destroy(context); return status == SLOPH_STATUS_OK ? 0 : 2;
}
