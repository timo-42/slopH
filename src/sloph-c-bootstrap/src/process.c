#include "sloph/native.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <signal.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

typedef struct Tail {
    unsigned char *data;
    size_t length;
    size_t limit;
} Tail;

static uint64_t monotonic_ns(void) {
    struct timespec value;
    if (clock_gettime(CLOCK_MONOTONIC, &value) != 0) return 0u;
    return (uint64_t)value.tv_sec * UINT64_C(1000000000) +
           (uint64_t)value.tv_nsec;
}

static int tail_add(Tail *tail, const unsigned char *data, size_t length) {
    size_t keep;
    if (tail->limit == 0u) return 1;
    if (length >= tail->limit) {
        if (tail->data == NULL) {
            tail->data = malloc(tail->limit + 1u);
            if (tail->data == NULL) return 0;
        }
        memcpy(tail->data, data + length - tail->limit, tail->limit);
        tail->length = tail->limit;
        return 1;
    }
    keep = tail->length;
    if (keep > tail->limit - length) keep = tail->limit - length;
    if (tail->data == NULL) {
        tail->data = malloc(tail->limit + 1u);
        if (tail->data == NULL) return 0;
    }
    if (keep != 0u && keep != tail->length)
        memmove(tail->data, tail->data + tail->length - keep, keep);
    memcpy(tail->data + keep, data, length);
    tail->length = keep + length;
    return 1;
}

static void close_fd(int *descriptor) {
    if (*descriptor >= 0) {
        while (close(*descriptor) != 0 && errno == EINTR) {}
        *descriptor = -1;
    }
}

void sloph_process_result_free(SlophProcessResult *result) {
    if (result == NULL) return;
    free(result->standard_output);
    free(result->standard_error);
    memset(result, 0, sizeof(*result));
}

SlophStatus sloph_process_run(SlophContext *context,
                              const SlophProcessOptions *options,
                              SlophProcessResult *out_result) {
    int output_pipe[2] = {-1, -1};
    int error_pipe[2] = {-1, -1};
    pid_t child;
    struct pollfd descriptors[2];
    Tail tails[2];
    size_t total = 0u;
    uint64_t deadline;
    int wait_status = 0;
    int child_done = 0;
    uint64_t child_done_at = 0u;
    int killed = 0;
    SlophStatus status = SLOPH_STATUS_OK;
    unsigned char chunk[8192];
    size_t environment_index;
    if (context == NULL || options == NULL || out_result == NULL ||
        options->arguments == NULL || options->arguments[0] == NULL ||
        options->timeout_seconds == 0u || options->output_limit == 0u ||
        options->output_limit == SIZE_MAX ||
        options->tail_limit == 0u || options->tail_limit == SIZE_MAX)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    if (options->environment_count != 0u && options->environment == NULL)
        return SLOPH_STATUS_INVALID_ARGUMENT;
    for (environment_index = 0u;
         environment_index < options->environment_count; ++environment_index) {
        const SlophProcessEnvironment *item =
            &options->environment[environment_index];
        if (item->name == NULL || *item->name == '\0' ||
            strchr(item->name, '=') != NULL || item->value == NULL)
            return SLOPH_STATUS_INVALID_ARGUMENT;
    }
    memset(out_result, 0, sizeof(*out_result));
    out_result->exit_code = -1;
    memset(tails, 0, sizeof(tails));
    tails[0].limit = options->tail_limit;
    tails[1].limit = options->tail_limit;
    if (pipe(output_pipe) != 0 || pipe(error_pipe) != 0) goto launch_failure;
    child = fork();
    if (child < 0) goto launch_failure;
    if (child == 0) {
        (void)setpgid(0, 0);
        for (environment_index = 0u;
             environment_index < options->environment_count;
             ++environment_index) {
            if (setenv(options->environment[environment_index].name,
                       options->environment[environment_index].value, 1) != 0)
                _exit(126);
        }
        if (options->working_directory != NULL &&
            chdir(options->working_directory) != 0) _exit(126);
        if (dup2(output_pipe[1], STDOUT_FILENO) < 0 ||
            dup2(error_pipe[1], STDERR_FILENO) < 0) _exit(126);
        close_fd(&output_pipe[0]); close_fd(&output_pipe[1]);
        close_fd(&error_pipe[0]); close_fd(&error_pipe[1]);
        execvp(options->arguments[0], (char *const *)options->arguments);
        _exit(errno == ENOENT ? 127 : 126);
    }
    (void)setpgid(child, child);
    close_fd(&output_pipe[1]); close_fd(&error_pipe[1]);
    {
        int output_flags = fcntl(output_pipe[0], F_GETFL);
        int error_flags = fcntl(error_pipe[0], F_GETFL);
        if (output_flags < 0 || error_flags < 0 ||
            fcntl(output_pipe[0], F_SETFL, output_flags | O_NONBLOCK) < 0 ||
            fcntl(error_pipe[0], F_SETFL, error_flags | O_NONBLOCK) < 0) {
            (void)kill(-child, SIGKILL);
            while (waitpid(child, &wait_status, 0) < 0 && errno == EINTR) {}
            close_fd(&output_pipe[0]); close_fd(&error_pipe[0]);
            (void)sloph_context_add_diagnostic_full(
                context, "compiler.process.pipe", "environment",
                "process diagnostic pipes could not be configured", "{}",
                SLOPH_UNKNOWN_SPAN, SLOPH_SEVERITY_ERROR);
            return SLOPH_STATUS_PROCESS_ERROR;
        }
    }
    descriptors[0].fd = output_pipe[0]; descriptors[0].events = POLLIN;
    descriptors[1].fd = error_pipe[0]; descriptors[1].events = POLLIN;
    deadline = monotonic_ns() + (uint64_t)options->timeout_seconds * UINT64_C(1000000000);
    while (!child_done || descriptors[0].fd >= 0 || descriptors[1].fd >= 0) {
        descriptors[0].revents = 0;
        descriptors[1].revents = 0;
        int poll_result = poll(descriptors, 2u, 50);
        size_t index;
        if (poll_result < 0) {
            if (errno == EINTR) continue;
            status = SLOPH_STATUS_PROCESS_ERROR; break;
        }
        for (index = 0u; index < 2u; ++index) {
            if (descriptors[index].fd < 0) continue;
            if ((descriptors[index].revents & (POLLIN | POLLHUP | POLLERR)) != 0) {
                for (;;) {
                    ssize_t count = read(descriptors[index].fd, chunk, sizeof(chunk));
                    if (count > 0) {
                        if ((size_t)count > options->output_limit -
                                                (total > options->output_limit
                                                     ? options->output_limit : total))
                            total = options->output_limit + 1u;
                        else
                            total += (size_t)count;
                        if (!tail_add(&tails[index], chunk, (size_t)count)) {
                            status = SLOPH_STATUS_OUT_OF_MEMORY; break;
                        }
                        if (total > options->output_limit && !killed) {
                            out_result->output_exceeded = 1; killed = 1;
                            (void)kill(-child, SIGKILL);
                        }
                    } else if (count == 0) {
                        close_fd(&descriptors[index].fd); break;
                    } else if (errno == EAGAIN || errno == EWOULDBLOCK) break;
                    else if (errno != EINTR) { close_fd(&descriptors[index].fd); break; }
                }
            }
            if (status != SLOPH_STATUS_OK) break;
        }
        if (status != SLOPH_STATUS_OK) break;
        if (!child_done) {
            pid_t waited = waitpid(child, &wait_status, WNOHANG);
            if (waited == child) {
                child_done = 1;
                child_done_at = monotonic_ns();
            }
            else if (waited < 0 && errno != EINTR) {
                status = SLOPH_STATUS_PROCESS_ERROR; break;
            }
        }
        if (!child_done && monotonic_ns() >= deadline && !killed) {
            out_result->timed_out = 1; killed = 1;
            (void)kill(-child, SIGKILL);
        }
        if (child_done &&
            (descriptors[0].fd >= 0 || descriptors[1].fd >= 0) &&
            monotonic_ns() - child_done_at >= UINT64_C(2000000000)) {
            out_result->pipe_timed_out = 1;
            (void)kill(-child, SIGKILL);
            close_fd(&descriptors[0].fd);
            close_fd(&descriptors[1].fd);
        }
    }
    if (status != SLOPH_STATUS_OK && !child_done) (void)kill(-child, SIGKILL);
    if (!child_done) while (waitpid(child, &wait_status, 0) < 0 && errno == EINTR) {}
    close_fd(&descriptors[0].fd); close_fd(&descriptors[1].fd);
    close_fd(&output_pipe[0]); close_fd(&error_pipe[0]);
    if (status != SLOPH_STATUS_OK) goto done;
    if (WIFEXITED(wait_status)) out_result->exit_code = WEXITSTATUS(wait_status);
    else if (WIFSIGNALED(wait_status)) out_result->exit_code = -WTERMSIG(wait_status);
    out_result->standard_output = tails[0].data; tails[0].data = NULL;
    out_result->standard_output_length = tails[0].length;
    out_result->standard_error = tails[1].data; tails[1].data = NULL;
    out_result->standard_error_length = tails[1].length;
    if (out_result->standard_output != NULL)
        out_result->standard_output[out_result->standard_output_length] = 0u;
    if (out_result->standard_error != NULL)
        out_result->standard_error[out_result->standard_error_length] = 0u;
    goto done;
launch_failure:
    (void)sloph_context_add_diagnostic_full(
        context, "compiler.process.launch", "environment",
        "process could not be started", "{}", SLOPH_UNKNOWN_SPAN,
        SLOPH_SEVERITY_ERROR);
    status = SLOPH_STATUS_PROCESS_ERROR;
done:
    close_fd(&output_pipe[0]); close_fd(&output_pipe[1]);
    close_fd(&error_pipe[0]); close_fd(&error_pipe[1]);
    free(tails[0].data); free(tails[1].data);
    if (status != SLOPH_STATUS_OK) sloph_process_result_free(out_result);
    return status;
}
