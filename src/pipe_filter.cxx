/*
 * Filter an istream through a piped program.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "pipe_filter.hxx"
#include "http_response.hxx"
#include "stopwatch.hxx"
#include "istream_stopwatch.hxx"
#include "strmap.hxx"
#include "pool.hxx"
#include "GException.hxx"
#include "istream/istream.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/IstreamSpawn.hxx"
#include "spawn/Prepared.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "util/ConstBuffer.hxx"
#include "util/HexFormat.h"
#include "util/djbhash.h"

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

template<typename A, typename E>
static const char *
make_pipe_etag(struct pool *pool, const char *in,
               const char *path,
               const A &args,
               const E &env)
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
            HttpResponseHandler &handler)
{
    /* need to hold this pool reference because it is guaranteed that
       the pool stays alive while the HttpResponseHandler runs, even
       if all other pool references are removed */
    const ScopePoolRef ref(*pool TRACE_ARGS);

    const char *etag;

    if (body == nullptr) {
        /* if the resource does not have a body (which is different
           from Content-Length:0), don't filter it */
        handler.InvokeResponse(status, std::move(headers), nullptr);
        return;
    }

    assert(!http_status_is_empty(status));

    auto *stopwatch = stopwatch_new(pool, path);

    PreparedChildProcess p;
    p.Append(path);
    for (auto i : args)
        p.Append(i);

    Istream *response;

    try {
        options.CopyTo(p, true, nullptr);
        SpawnChildProcess(event_loop, pool, path, body, &response,
                          std::move(p),
                          spawn_service);
    } catch (...) {
        handler.InvokeError(ToGError(std::current_exception()));
        return;
    }

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

    handler.InvokeResponse(status, std::move(headers), response);
}
