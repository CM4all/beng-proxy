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

struct sink_gstring {
    struct pool *pool;
    struct istream *input;

    GString *value;

    void (*callback)(GString *value, GError *error, void *ctx);
    void *callback_ctx;

    struct async_operation operation;

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
    auto sg = NewFromPool<struct sink_gstring>(*pool);

    sg->pool = pool;

    istream_assign_handler(&sg->input, input,
                           &MakeIstreamHandler<struct sink_gstring>::handler, sg,
                           FD_ANY);

    sg->value = g_string_sized_new(256);
    sg->callback = callback;
    sg->callback_ctx = ctx;

    sg->operation.Init2<struct sink_gstring>();
    async_ref->Set(sg->operation);
}
