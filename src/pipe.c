/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe.h"
#include "http-response.h"
#include "fork.h"
#include "format.h"

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

static const char *
append_etag(pool_t pool, const char *in, const char *suffix)
{
    size_t length;

    if (*in != '"')
        /* simple concatenation */
        return p_strcat(pool, in, suffix, NULL);

    length = strlen(in + 1);
    if (in[length] != '"')
        return p_strcat(pool, in, suffix, NULL);

    return p_strncat(pool, in, length, suffix, strlen(suffix),
                     "\"", 1, NULL);
}

static inline unsigned
calc_hash(const char *p) {
    unsigned hash = 5381;

    assert(p != NULL);

    while (*p != 0)
        hash = (hash << 5) + hash + *p++;

    return hash;
}

static const char *
make_pipe_etag(pool_t pool, const char *in,
               const char *path,
               const char *const* args, unsigned num_args)
{
    char suffix[10] = {'-'};

    /* build hash from path and arguments */
    unsigned hash = calc_hash(path);

    for (unsigned i = 0; i < num_args; ++i)
        hash ^= calc_hash(args[i]);

    format_uint32_hex_fixed(suffix + 1, hash);
    suffix[9] = 0;

    /* append the hash to the old ETag */
    return append_etag(pool, in, suffix);
}

void
pipe_filter(pool_t pool, const char *path,
            const char *const* args, unsigned num_args,
            http_status_t status, struct strmap *headers, istream_t body,
            const struct http_response_handler *handler,
            void *handler_ctx)
{
    pid_t pid;
    istream_t response;
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

    etag = strmap_get(headers, "etag");
    if (etag != NULL) {
        /* we cannot pass the original ETag to the client, because the
           pipe has modified the resource (which is what the pipe is
           all about) - append a digest value to the ETag, which is
           built from the program path and its arguments */

        etag = make_pipe_etag(pool, etag, path, args, num_args);
        assert(etag != NULL);

        headers = strmap_dup(pool, headers);
        strmap_set(headers, "etag", etag);
    }

    http_response_handler_direct_response(handler, handler_ctx, status,
                                          headers, response);
}
