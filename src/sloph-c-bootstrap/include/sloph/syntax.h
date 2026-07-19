#ifndef SLOPH_SYNTAX_H
#define SLOPH_SYNTAX_H

#include "sloph/base.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlophContext SlophContext;
typedef struct SlophSyntaxModule SlophSyntaxModule;

typedef struct SlophSyntaxText {
    char *data;
    size_t length;
} SlophSyntaxText;

SlophStatus sloph_syntax_parse(SlophContext *context,
                               const unsigned char *source,
                               size_t source_length, unsigned version,
                               SlophSyntaxModule **out_module);
SlophStatus sloph_syntax_validate(SlophContext *context,
                                  const SlophSyntaxModule *module);
SlophStatus sloph_syntax_format(SlophContext *context,
                                const SlophSyntaxModule *module,
                                SlophSyntaxText *out_text);
SlophStatus sloph_syntax_to_json(SlophContext *context,
                                 const SlophSyntaxModule *module,
                                 SlophSyntaxText *out_json);
SlophStatus sloph_syntax_from_json(SlophContext *context,
                                   const unsigned char *json,
                                   size_t json_length, unsigned version,
                                   SlophSyntaxModule **out_module);
void sloph_syntax_text_free(SlophContext *context, SlophSyntaxText *text);
void sloph_syntax_module_free(SlophSyntaxModule *module);

#ifdef __cplusplus
}
#endif
#endif
