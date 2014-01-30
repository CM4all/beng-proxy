/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe.h"
#include "http_response.h"
#include "fork.h"
#include "format.h"
#include "stopwatch.h"
#include "strmap.h"
#include "pool.h"
#include "istream.h"
#include "sigutil.h"
#include "namespace_options.h"
#include "djbhash.h"

#include <daemon/log.h>

#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <string.h>
#include <errno.h>

static void
pipe_child_callback(int status, void *ctx gcc_unused)
{
    int exit_status = WEXITSTATUS(status);

    if (WIFSIGNALED(status)) {
        int level = 1;
        if (!WCOREDUMP(status) && WTERMSIG(status) == SIGTERM)
            level = 4;

        daemon_log(level, "Pipe program died from signal %d%s\n",
                   WTERMSIG(status),
                   WCOREDUMP(status) ? " (core dumped)" : "");
    } else if (exit_status != 0)
        daemon_log(1, "Pipe program exited with status %d\n",
                   exit_status);
}

static const char *
append_etag(struct pool *pool, const char *in, const char *suffix)
{
    size_t length;

    if (*in != '"')
        /* simple concatenation */
        return p_strcat(pool, in, suffix, NULL);

    length = strlen(in + 1);
    if (in[length] != '"')
        return p_strcat(pool, in, suffix, NULL);

    return p_strncat(pool, in, length, suffix, strlen(suffix),
                     "\"", (size_t)1, NULL);
}

static const char *
make_pipe_etag(struct pool *pool, const char *in,
               const char *path,
               const char *const* args, unsigned num_args)
{
    char suffix[10] = {'-'};

    /* build hash from path and arguments */
    unsigned hash = djb_hash_string(path);

    for (unsigned i = 0; i < num_args; ++i)
        hash ^= djb_hash_string(args[i]);

    format_uint32_hex_fixed(suffix + 1, hash);
    suffix[9] = 0;

    /* append the hash to the old ETag */
    return append_etag(pool, in, suffix);
}

void
pipe_filter(struct pool *pool, const char *path,
            const char *const* args, unsigned num_args,
            const struct namespace_options *ns,
            http_status_t status, struct strmap *headers, struct istream *body,
            const struct http_response_handler *handler,
            void *handler_ctx)
{
    struct stopwatch *stopwatch;
    pid_t pid;
    struct istream *response;
    char *argv[1 + num_args + 1];
    const char *etag;

    if (body == NULL) {
        /* if the resource does not have a body (which is different
           from Content-Length:0), don't filter it */
        http_response_handler_direct_response(handler, handler_ctx,
                                              status, headers, NULL);
        return;
    }

    assert(!http_status_is_empty(status));

    stopwatch = stopwatch_new(pool, path);

    /* avoid race condition due to libevent signal handler in child
       process */
    sigset_t signals;
    enter_signal_section(&signals);

    GError *error = NULL;
    pid = beng_fork(pool, path, body, &response,
                    pipe_child_callback, NULL, &error);
    if (pid < 0) {
        leave_signal_section(&signals);

        istream_close_unused(body);
        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    argv[0] = p_strdup(pool, path);
    memcpy(argv + 1, args, num_args * sizeof(argv[0]));
    argv[1 + num_args] = NULL;

    if (pid == 0) {
        install_default_signal_handlers();
        leave_signal_section(&signals);

        namespace_options_unshare(ns);
        namespace_options_setup(ns);

        execv(path, argv);
        fprintf(stderr, "exec('%s') failed: %s\n",
                path, strerror(errno));
        _exit(2);
    }

    leave_signal_section(&signals);

    stopwatch_event(stopwatch, "fork");

    etag = strmap_get(headers, "etag");
    if (etag != NULL) {
        /* we cannot pass the original ETag to the client, because the
           pipe has modified the resource (which is what the pipe is
           all about) - append a digest value to the ETag, which is
           built from the program path and its arguments */

        etag = make_pipe_etag(pool, etag, path, args, num_args);
        assert(etag != NULL);

        headers = strmap_dup(pool, headers, 17);
        strmap_set(headers, "etag", etag);
    }

    response = istream_stopwatch_new(pool, response, stopwatch);

    http_response_handler_direct_response(handler, handler_ctx, status,
                                          headers, response);
}
