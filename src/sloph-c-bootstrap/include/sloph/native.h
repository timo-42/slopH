#ifndef SLOPH_NATIVE_H
#define SLOPH_NATIVE_H

#include "sloph/compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct SlophProcessEnvironment {
    const char *name;
    const char *value;
} SlophProcessEnvironment;

typedef struct SlophProcessOptions {
    const char *const *arguments;
    const char *working_directory;
    unsigned int timeout_seconds;
    size_t output_limit;
    size_t tail_limit;
    const SlophProcessEnvironment *environment;
    size_t environment_count;
} SlophProcessOptions;

typedef struct SlophProcessResult {
    int exit_code;
    int timed_out;
    int pipe_timed_out;
    int output_exceeded;
    unsigned char *standard_output;
    size_t standard_output_length;
    unsigned char *standard_error;
    size_t standard_error_length;
} SlophProcessResult;

typedef struct SlophNativeTimings {
    uint64_t core_to_c_nanoseconds;
    uint64_t c_compile_link_nanoseconds;
} SlophNativeTimings;

typedef struct SlophNativeOptions {
    const char *compiler;
    const char *output_path;
    const char *emit_c_path;
    SlophNativeTimings *timings;
} SlophNativeOptions;

SlophNativeOptions sloph_native_options_default(void);
SlophStatus sloph_process_run(SlophContext *context,
                              const SlophProcessOptions *options,
                              SlophProcessResult *out_result);
void sloph_process_result_free(SlophProcessResult *result);
SlophStatus sloph_native_compile(SlophContext *context,
                                 SlophCoreUnit *heartwood,
                                 const SlophProject *project,
                                 const char *symbol,
                                 const SlophNativeOptions *options);

#ifdef __cplusplus
}
#endif
#endif
