#include "istream.h"
#include "istream-invoke.h"
#include "istream-new.h"

static struct istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo_bar_0123456789abcdefghijklmnopqrstuvwxyz");
}

static struct istream *
create_test(struct pool *pool, struct istream *input)
{
    return istream_chunked_new(pool, input);
}

#define CUSTOM_TEST

struct custom {
    struct istream output;

    bool eof;
    GError *error;
};

/*
 * istream handler
 *
 */

static size_t
custom_istream_data(gcc_unused const void *data, gcc_unused size_t length,
                    void *_ctx)
{
    struct custom *ctx = _ctx;

    istream_invoke_data(&ctx->output, " ", 1);
    return 0;
}

static void
custom_istream_eof(void *_ctx)
{
    struct custom *ctx = _ctx;

    ctx->eof = true;
}

static void
custom_istream_abort(GError *error, void *_ctx)
{
    struct custom *ctx = _ctx;

    ctx->error = error;
}

static const struct istream_handler custom_istream_handler = {
    .data = custom_istream_data,
    .eof = custom_istream_eof,
    .abort = custom_istream_abort,
};

/*
 * istream class
 *
 */

static off_t
istream_custom_available(gcc_unused struct istream *istream,
                         gcc_unused bool partial)
{
    return 1;
}

static void
istream_custom_read(gcc_unused struct istream *istream)
{
}

static void
istream_custom_close(struct istream *istream)
{
    istream_deinit(istream);
}

static const struct istream_class istream_custom = {
    .available = istream_custom_available,
    .read = istream_custom_read,
    .close = istream_custom_close,
};

static void
test_custom(struct pool *pool)
{
    pool = pool_new_linear(pool, "test", 8192);
    struct custom *ctx = p_malloc(pool, sizeof(*ctx));
    istream_init(&ctx->output, &istream_custom, pool);

    struct istream *chunked = istream_chunked_new(pool, &ctx->output);
    istream_handler_set(chunked, &custom_istream_handler, ctx, 0);
    pool_unref(pool);

    istream_read(chunked);
    istream_close(chunked);

    pool_commit();
}

#include "t-istream-filter.h"
