/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_gstring.hxx"
#include "Sink.hxx"
#include "async.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <glib.h>

struct GStringSink final : IstreamSink, Cancellable {
    struct pool *pool;

    GString *value;

    void (*callback)(GString *value, GError *error, void *ctx);
    void *callback_ctx;

    GStringSink(struct pool &_pool, Istream &_input,
                void (*_callback)(GString *value, GError *error, void *ctx),
                void *_ctx,
                struct async_operation_ref &async_ref)
        :IstreamSink(_input, FD_ANY), pool(&_pool),
         value(g_string_sized_new(256)),
         callback(_callback), callback_ctx(_ctx) {
        async_ref = *this;
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        g_string_free(value, true);

        const ScopePoolRef ref(*pool TRACE_ARGS);
        input.Close();
    }

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override {
        g_string_append_len(value, (const char *)data, length);
        return length;
    }

    void OnEof() override {
        callback(value, nullptr, callback_ctx);
    }

    void OnError(GError *error) override {
        g_string_free(value, true);
        callback(nullptr, error, callback_ctx);
    }
};

/*
 * constructor
 *
 */

void
sink_gstring_new(struct pool &pool, Istream &input,
                 void (*callback)(GString *value, GError *error, void *ctx),
                 void *ctx, struct async_operation_ref &async_ref)
{
    NewFromPool<GStringSink>(pool, pool, input,
                             callback, ctx,
                             async_ref);
}
