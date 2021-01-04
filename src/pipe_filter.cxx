/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "pipe_filter.hxx"
#include "HttpResponseHandler.hxx"
#include "stopwatch.hxx"
#include "istream_stopwatch.hxx"
#include "strmap.hxx"
#include "AllocatorPtr.hxx"
#include "istream/UnusedPtr.hxx"
#include "spawn/ChildOptions.hxx"
#include "spawn/IstreamSpawn.hxx"
#include "spawn/Prepared.hxx"
#include "util/ConstBuffer.hxx"
#include "util/HexFormat.h"
#include "util/djbhash.h"

#include <stdio.h>
#include <string.h>

static const char *
append_etag(AllocatorPtr alloc, const char *in, const char *suffix)
{
    size_t length;

    if (*in != '"')
        /* simple concatenation */
        return alloc.Concat(in, suffix);

    length = strlen(in + 1);
    if (in[length] != '"')
        return alloc.Concat(in, suffix);

    return alloc.Concat(StringView{in, length},
                        suffix,
                        '"');
}

template<typename A, typename E>
static const char *
make_pipe_etag(AllocatorPtr alloc, const char *in,
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
    return append_etag(alloc, in, suffix);
}

void
pipe_filter(SpawnService &spawn_service, EventLoop &event_loop,
            struct pool &pool,
            const StopwatchPtr &parent_stopwatch,
            const char *path,
            ConstBuffer<const char *> args,
            const ChildOptions &options,
            http_status_t status, StringMap &&headers, UnusedIstreamPtr body,
            HttpResponseHandler &handler)
{
    const char *etag;

    if (!body) {
        /* if the resource does not have a body (which is different
           from Content-Length:0), don't filter it */
        handler.InvokeResponse(status, std::move(headers), UnusedIstreamPtr());
        return;
    }

    assert(!http_status_is_empty(status));

    StopwatchPtr stopwatch(parent_stopwatch, path);

    PreparedChildProcess p;
    p.Append(path);
    for (auto i : args)
        p.Append(i);

    UnusedIstreamPtr response;

    try {
        options.CopyTo(p);
        response = SpawnChildProcess(event_loop, &pool, path, std::move(body),
                                     std::move(p),
                                     spawn_service);
    } catch (...) {
        handler.InvokeError(std::current_exception());
        return;
    }

    stopwatch.RecordEvent("fork");

    etag = headers.Remove("etag");
    if (etag != nullptr) {
        /* we cannot pass the original ETag to the client, because the
           pipe has modified the resource (which is what the pipe is
           all about) - append a digest value to the ETag, which is
           built from the program path and its arguments */

        etag = make_pipe_etag(pool, etag, path, args,
                              options.env);
        assert(etag != nullptr);

        headers.Add(pool, "etag", etag);
    }

    response = istream_stopwatch_new(pool, std::move(response),
                                     std::move(stopwatch));

    handler.InvokeResponse(status, std::move(headers), std::move(response));
}
