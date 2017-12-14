/*
 * Copyright 2007-2017 Content Management AG
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

#include "PInstance.hxx"
#include "memcached/memcached_client.hxx"
#include "http_cache_document.hxx"
#include "lease.hxx"
#include "system/SetupProcess.hxx"
#include "strmap.hxx"
#include "tpool.hxx"
#include "serialize.hxx"
#include "istream/sink_buffer.hxx"
#include "istream/istream.hxx"
#include "direct.hxx"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "net/RConnectSocket.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cancellable.hxx"
#include "util/PrintException.hxx"

#include <unistd.h>
#include <stdio.h>
#include <errno.h>
#include <string.h>

struct Context final : PInstance, Lease {
    struct pool *pool;

    UniqueSocketDescriptor s;
    bool idle = false, reuse;

    bool success = false;

    CancellablePointer cancel_ptr;

    /* virtual methods from class Lease */
    void ReleaseLease(bool _reuse) noexcept override {
        assert(!idle);
        assert(s.IsDefined());

        idle = true;
        reuse = _reuse;

        s.Close();
    }
};

static void
dump_choice(const HttpCacheDocument *document)
{
    const auto delta = document->info.expires - std::chrono::system_clock::now();
    const auto delta_s = std::chrono::duration_cast<std::chrono::seconds>(delta);
    printf("expires=%ld\n", (long)delta_s.count());

    for (const auto &i : document->vary)
        printf("\t%s: %s\n", i.key, i.value);
}

/*
 * sink_buffer callback
 *
 */

static void
my_sink_done(void *data0, size_t length, void *_ctx)
{
    auto &ctx = *(Context *)_ctx;

    /*uint32_t magic;*/

    ConstBuffer<void> data(data0, length);

    try {
        while (!data.empty()) {
            const AutoRewindPool auto_rewind(*tpool);
            HttpCacheDocument document(*tpool);

            /*magic = */deserialize_uint32(data);
            /*
              if (magic != CHOICE_MAGIC)
              break;
            */

            document.info.expires = std::chrono::system_clock::from_time_t(deserialize_uint64(data));
            deserialize_strmap(data, document.vary);

            dump_choice(&document);
        }
    } catch (DeserializeError) {
        return;
    }

    ctx.success = true;
}

static void
my_sink_error(std::exception_ptr ep, gcc_unused void *ctx)
{
    PrintException(ep);
}

static const struct sink_buffer_handler my_sink_handler = {
    .done = my_sink_done,
    .error = my_sink_error,
};


/*
 * memcached_response_handler_t
 *
 */

static void
my_mcd_response(enum memcached_response_status status,
                gcc_unused const void *extras,
                gcc_unused size_t extras_length,
                gcc_unused const void *key,
                gcc_unused size_t key_length,
                Istream *value, void *ctx)
{
    auto *c = (Context *)ctx;

    if (status != MEMCACHED_STATUS_NO_ERROR || value == NULL) {
        fprintf(stderr, "status=%d\n", status);
        if (value != NULL)
            value->CloseUnused();
        return;
    }

    sink_buffer_new(*c->pool, *value,
                    my_sink_handler, c,
                    c->cancel_ptr);
}

static void
my_mcd_error(std::exception_ptr ep, gcc_unused void *ctx)
{
    PrintException(ep);
}

static const struct memcached_client_handler my_mcd_handler = {
    .response = my_mcd_response,
    .error = my_mcd_error,
};

/*
 * main
 *
 */

int main(int argc, char **argv) {
    const char *key;

    if (argc != 3) {
        fprintf(stderr, "usage: run-memcached-client HOST[:PORT] URI\n");
        return 1;
    }

    direct_global_init();
    const ScopeFbPoolInit fb_pool_init;

    Context ctx;

    /* connect socket */

    ctx.s = ResolveConnectStreamSocket(argv[1], 11211);
    ctx.s.SetNoDelay();

    /* initialize */

    SetupProcess();

    ctx.pool = pool_new_linear(ctx.root_pool, "test", 8192);

    key = p_strcat(ctx.pool, argv[2], " choice", NULL);
    printf("key='%s'\n", key);

    /* send memcached request */

    memcached_client_invoke(ctx.pool, ctx.event_loop,
                            ctx.s, FdType::FD_TCP,
                            ctx,
                            MEMCACHED_OPCODE_GET,
                            NULL, 0,
                            key, strlen(key),
                            NULL,
                            &my_mcd_handler, &ctx,
                            ctx.cancel_ptr);
    pool_unref(ctx.pool);
    pool_commit();

    ctx.event_loop.Dispatch();

    /* cleanup */

    return ctx.success ? 0 : 2;
}
