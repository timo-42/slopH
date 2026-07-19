#include "sloph/sloph.h"

#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <unistd.h>

static int child_mode(const char *mode) {
    if (strcmp(mode, "--child-ok") == 0) {
        fputs("out", stdout); fputs("err", stderr); return 7;
    }
    if (strcmp(mode, "--child-output") == 0) {
        fputs("0123456789abcdef", stdout); return 0;
    }
    if (strcmp(mode, "--child-wait") == 0) {
        sleep(2u); return 0;
    }
    if (strcmp(mode, "--child-inherit") == 0) {
        pid_t child = fork();
        if (child < 0) return 8;
        if (child == 0) { sleep(5u); _exit(0); }
        return 0;
    }
    return -1;
}

int main(int argc, char **argv) {
    SlophContext *context = NULL;
    SlophContextConfig config = sloph_context_config_default();
    SlophProcessOptions options;
    SlophProcessResult result;
    const char *arguments[3];
    int mode;
    if (argc == 2 && (mode = child_mode(argv[1])) >= 0) return mode;
    if (sloph_context_create(&config, &context) != SLOPH_STATUS_OK) return 1;
    memset(&options, 0, sizeof(options));
    arguments[0] = argv[0]; arguments[2] = NULL;
    options.arguments = arguments;
    options.timeout_seconds = 3u;
    options.output_limit = 1024u;
    options.tail_limit = 64u;
    arguments[1] = "--child-ok";
    if (sloph_process_run(context, &options, &result) != SLOPH_STATUS_OK ||
        result.exit_code != 7 || result.timed_out || result.output_exceeded ||
        result.standard_output_length != 3u ||
        memcmp(result.standard_output, "out", 3u) != 0 ||
        result.standard_error_length != 3u ||
        memcmp(result.standard_error, "err", 3u) != 0) return 2;
    sloph_process_result_free(&result);
    options.output_limit = SIZE_MAX;
    if (sloph_process_run(context, &options, &result) !=
        SLOPH_STATUS_INVALID_ARGUMENT) return 6;
    options.output_limit = 1024u;
    {
        const SlophProcessEnvironment invalid_environment[] = {{"BAD=NAME", "x"}};
        options.environment = invalid_environment;
        options.environment_count = 1u;
        if (sloph_process_run(context, &options, &result) !=
            SLOPH_STATUS_INVALID_ARGUMENT) return 7;
        options.environment = NULL;
        options.environment_count = 0u;
    }
    arguments[1] = "--child-inherit"; options.timeout_seconds = 5u;
    if (sloph_process_run(context, &options, &result) != SLOPH_STATUS_OK ||
        !result.pipe_timed_out) return 5;
    sloph_process_result_free(&result);
    arguments[1] = "--child-output"; options.output_limit = 4u;
    if (sloph_process_run(context, &options, &result) != SLOPH_STATUS_OK ||
        !result.output_exceeded) return 3;
    sloph_process_result_free(&result);
    arguments[1] = "--child-wait"; options.output_limit = 1024u;
    options.timeout_seconds = 1u;
    if (sloph_process_run(context, &options, &result) != SLOPH_STATUS_OK ||
        !result.timed_out) return 4;
    sloph_process_result_free(&result);
    sloph_context_destroy(context);
    return 0;
}
