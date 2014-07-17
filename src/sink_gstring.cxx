/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "sink_gstring.hxx"
#include "async.hxx"
#include "istream.h"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <glib.h>

struct sink_gstring {
    struct pool *pool;
    struct istream *input;

    GString *value;

    void (*callback)(GString *value, GError *error, void *ctx);
    void *callback_ctx;

    struct async_operation async_operation;
};

/*
 * istream handler
 *
 */

static size_t
sink_gstring_input_data(const void *data, size_t length, void *ctx)
{
    struct sink_gstring *sg = (struct sink_gstring *)ctx;

    g_string_append_len(sg->value, (const char *)data, length);
    return length;
}

static void
sink_gstring_input_eof(void *ctx)
{
    struct sink_gstring *sg = (struct sink_gstring *)ctx;

    sg->async_operation.Finished();

    sg->callback(sg->value, nullptr, sg->callback_ctx);
}

static void
sink_gstring_input_abort(GError *error, void *ctx)
{
    struct sink_gstring *sg = (struct sink_gstring *)ctx;

    sg->async_operation.Finished();

    g_string_free(sg->value, true);
    sg->callback(nullptr, error, sg->callback_ctx);
}

static const struct istream_handler sink_gstring_input_handler = {
    .data = sink_gstring_input_data,
    .eof = sink_gstring_input_eof,
    .abort = sink_gstring_input_abort,
};


/*
 * async operation
 *
 */

static struct sink_gstring *
async_to_sink_gstring(struct async_operation *ao)
{
    return &ContainerCast2(*ao, &sink_gstring::async_operation);
}

static void
sink_gstring_async_abort(struct async_operation *ao)
{
    struct sink_gstring *sg = async_to_sink_gstring(ao);

    g_string_free(sg->value, true);

    const ScopePoolRef ref(*sg->pool TRACE_ARGS);
    istream_close_handler(sg->input);
}

static const struct async_operation_class sink_gstring_operation = {
    .abort = sink_gstring_async_abort,
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
                           &sink_gstring_input_handler, sg,
                           ISTREAM_ANY);

    sg->value = g_string_sized_new(256);
    sg->callback = callback;
    sg->callback_ctx = ctx;

    sg->async_operation.Init(sink_gstring_operation);
    async_ref->Set(sg->async_operation);
}
