/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_gstring.hxx"
#include "istream.hxx"
#include "async.hxx"
#include "istream_oo.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <glib.h>

struct GStringSink {
    struct pool *pool;
    struct istream *input;

    GString *value;

    void (*callback)(GString *value, GError *error, void *ctx);
    void *callback_ctx;

    struct async_operation operation;

    GStringSink(struct pool &_pool, struct istream &_input,
                void (*_callback)(GString *value, GError *error, void *ctx),
                void *_ctx,
                struct async_operation_ref &async_ref)
        :pool(&_pool),
         value(g_string_sized_new(256)),
         callback(_callback), callback_ctx(_ctx) {
        istream_assign_handler(&input, &_input,
                               &MakeIstreamHandler<GStringSink>::handler, this,
                               FD_ANY);

        operation.Init2<GStringSink>();
        async_ref.Set(operation);
    }

    void Abort() {
        g_string_free(value, true);

        const ScopePoolRef ref(*pool TRACE_ARGS);
        istream_close_handler(input);
    }

    /* istream handler */

    size_t OnData(const void *data, size_t length) {
        g_string_append_len(value, (const char *)data, length);
        return length;
    }

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof() {
        operation.Finished();
        callback(value, nullptr, callback_ctx);
    }

    void OnError(GError *error) {
        operation.Finished();
        g_string_free(value, true);
        callback(nullptr, error, callback_ctx);
    }
};

/*
 * constructor
 *
 */

void
sink_gstring_new(struct pool *pool, struct istream *input,
                 void (*callback)(GString *value, GError *error, void *ctx),
                 void *ctx, struct async_operation_ref *async_ref)
{
    NewFromPool<GStringSink>(*pool, *pool, *input,
                             callback, ctx,
                             *async_ref);
}
