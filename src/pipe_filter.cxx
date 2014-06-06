/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe_filter.hxx"
#include "http_response.h"
#include "fork.h"
#include "format.h"
#include "stopwatch.h"
#include "strmap.h"
#include "pool.h"
#include "istream.h"
#include "sigutil.h"
#include "child_options.hxx"
#include "djbhash.h"
#include "exec.hxx"
#include "util/ConstBuffer.hxx"

#include <daemon/log.h>

#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

struct pipe_ctx {
    const struct child_options &options;

    sigset_t signals;

    Exec exec;

    ConstBuffer<const char *> env;
};

static int
pipe_fn(void *ctx)
{
    struct pipe_ctx *c = (struct pipe_ctx *)ctx;

    install_default_signal_handlers();
    leave_signal_section(&c->signals);

    c->options.SetupStderr();
    namespace_options_setup(&c->options.ns);
    rlimit_options_apply(&c->options.rlimits);

    clearenv();

    for (auto i : c->env)
        putenv(const_cast<char *>(i));

    c->exec.DoExec();
}

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
        return p_strcat(pool, in, suffix, nullptr);

    length = strlen(in + 1);
    if (in[length] != '"')
        return p_strcat(pool, in, suffix, nullptr);

    return p_strncat(pool, in, length, suffix, strlen(suffix),
                     "\"", (size_t)1, nullptr);
}

static const char *
make_pipe_etag(struct pool *pool, const char *in,
               const char *path,
               ConstBuffer<const char *> args,
               ConstBuffer<const char *> env)
{
    char suffix[10] = {'-'};

    /* build hash from path and arguments */
    unsigned hash = djb_hash_string(path);

    for (auto i : args)
        hash ^= djb_hash_string(i);

    for (auto i : env)
        hash ^= djb_hash_string(i);

    format_uint32_hex_fixed(suffix + 1, hash);
    suffix[9] = 0;

    /* append the hash to the old ETag */
    return append_etag(pool, in, suffix);
}

void
pipe_filter(struct pool *pool, const char *path,
            ConstBuffer<const char *> args,
            ConstBuffer<const char *> env,
            const struct child_options &options,
            http_status_t status, struct strmap *headers, struct istream *body,
            const struct http_response_handler *handler,
            void *handler_ctx)
{
    struct stopwatch *stopwatch;
    struct istream *response;
    const char *etag;

    if (body == nullptr) {
        /* if the resource does not have a body (which is different
           from Content-Length:0), don't filter it */
        http_response_handler_direct_response(handler, handler_ctx,
                                              status, headers, nullptr);
        return;
    }

    assert(!http_status_is_empty(status));

    stopwatch = stopwatch_new(pool, path);

    struct pipe_ctx c = {
        .options = options,
        .env = env,
    };

    c.exec.Append(path);
    for (auto i : args)
        c.exec.Append(i);

    const int clone_flags =
        namespace_options_clone_flags(&options.ns, SIGCHLD);

    /* avoid race condition due to libevent signal handler in child
       process */
    enter_signal_section(&c.signals);

    GError *error = nullptr;
    pid_t pid = beng_fork(pool, path, body, &response,
                          clone_flags,
                          pipe_fn, &c,
                          pipe_child_callback, nullptr, &error);
    if (pid < 0) {
        leave_signal_section(&c.signals);

        istream_close_unused(body);
        http_response_handler_direct_abort(handler, handler_ctx, error);
        return;
    }

    leave_signal_section(&c.signals);

    stopwatch_event(stopwatch, "fork");

    etag = strmap_get(headers, "etag");
    if (etag != nullptr) {
        /* we cannot pass the original ETag to the client, because the
           pipe has modified the resource (which is what the pipe is
           all about) - append a digest value to the ETag, which is
           built from the program path and its arguments */

        etag = make_pipe_etag(pool, etag, path, args, env);
        assert(etag != nullptr);

        headers = strmap_dup(pool, headers, 17);
        strmap_set(headers, "etag", etag);
    }

    response = istream_stopwatch_new(pool, response, stopwatch);

    http_response_handler_direct_response(handler, handler_ctx, status,
                                          headers, response);
}
