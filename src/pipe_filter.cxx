/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe_filter.hxx"
#include "http_response.hxx"
#include "format.h"
#include "stopwatch.hxx"
#include "istream_stopwatch.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "istream/istream.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/IstreamSpawn.hxx"
#include "spawn/Prepared.hxx"
#include "PrefixLogger.hxx"
#include "util/ConstBuffer.hxx"
#include "util/djbhash.h"
#include "util/Error.hxx"

#include <daemon/log.h>

#include <sys/wait.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

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
pipe_filter(SpawnService &spawn_service, EventLoop &event_loop,
            struct pool *pool, const char *path,
            ConstBuffer<const char *> args,
            const ChildOptions &options,
            http_status_t status, StringMap &&headers, Istream *body,
            const struct http_response_handler *handler,
            void *handler_ctx)
{
    const char *etag;

    if (body == nullptr) {
        /* if the resource does not have a body (which is different
           from Content-Length:0), don't filter it */
        handler->InvokeResponse(handler_ctx, status, std::move(headers),
                                nullptr);
        return;
    }

    assert(!http_status_is_empty(status));

    auto *stopwatch = stopwatch_new(pool, path);

    const auto prefix_logger = CreatePrefixLogger(event_loop, IgnoreError());

    PreparedChildProcess p;
    p.stderr_fd = prefix_logger.second;
    p.Append(path);
    for (auto i : args)
        p.Append(i);

    GError *error = nullptr;
    if (!options.CopyTo(p, true, nullptr, &error)) {
        DeletePrefixLogger(prefix_logger.first);
        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    Istream *response;
    pid_t pid = SpawnChildProcess(event_loop, pool, path, body, &response,
                                  std::move(p),
                                  spawn_service, &error);
    if (prefix_logger.second >= 0)
        close(prefix_logger.second);
    if (pid < 0) {
        DeletePrefixLogger(prefix_logger.first);
        handler->InvokeAbort(handler_ctx, error);
        return;
    }

    if (prefix_logger.first != nullptr)
        PrefixLoggerSetPid(*prefix_logger.first, pid);

    stopwatch_event(stopwatch, "fork");

    etag = headers.Get("etag");
    if (etag != nullptr) {
        /* we cannot pass the original ETag to the client, because the
           pipe has modified the resource (which is what the pipe is
           all about) - append a digest value to the ETag, which is
           built from the program path and its arguments */

        etag = make_pipe_etag(pool, etag, path, args,
                              options.env);
        assert(etag != nullptr);

        headers.Set("etag", etag);
    }

    response = istream_stopwatch_new(*pool, *response, stopwatch);

    handler->InvokeResponse(handler_ctx, status, std::move(headers), response);
}
