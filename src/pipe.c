/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe.h"
#include "http-response.h"
#include "fork.h"
#include "fork.h"

#include <daemon/log.h>

#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static void
pipe_child_callback(int status, void *ctx __attr_unused)
{
    int exit_status = WEXITSTATUS(status);

    if (WIFSIGNALED(status)) {
        daemon_log(1, "Pipe program died from signal %d%s\n",
                   WTERMSIG(status),
                   WCOREDUMP(status) ? " (core dumped)" : "");
    } else if (exit_status != 0)
        daemon_log(1, "Pipe program exited with status %d\n",
                   exit_status);
}

void
pipe_filter(pool_t pool, const char *path,
            const char *const* args, unsigned num_args,
            struct strmap *headers, istream_t body,
            const struct http_response_handler *handler,
            void *handler_ctx)
{
    pid_t pid;
    istream_t response;
    char *argv[1 + num_args + 1];

    pid = beng_fork(pool, body, &response,
                    pipe_child_callback, NULL);
    if (pid < 0) {
        istream_close(body);
        http_response_handler_direct_abort(handler, handler_ctx);
        return;
    }

    argv[0] = p_strdup(pool, path);
    memcpy(argv + 1, args, num_args * sizeof(argv[0]));
    argv[1 + num_args] = NULL;

    if (pid == 0) {
        execv(path, argv);
        fprintf(stderr, "exec('%s') failed: %s\n",
                path, strerror(errno));
        _exit(2);
    }

    http_response_handler_direct_response(handler, handler_ctx, HTTP_STATUS_OK,
                                          headers, response);
}
