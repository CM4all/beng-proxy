#include "istream/istream_chunked.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_oo.hxx"
#include "pool.hxx"

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

struct Custom {
    struct istream output;

    bool eof;
    GError *error;

    /* istream handler */
    size_t OnData(gcc_unused const void *data, gcc_unused size_t length) {
        istream_invoke_data(&output, " ", 1);
        return 0;
    }

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof() {
        eof = true;
    }

    void OnError(GError *_error) {
        error = _error;
    }

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
    .skip = nullptr,
    .read = istream_custom_read,
    .as_fd = nullptr,
    .close = istream_custom_close,
};

static void
test_custom(struct pool *pool)
{
    pool = pool_new_linear(pool, "test", 8192);
    auto *ctx = NewFromPool<Custom>(*pool);
    istream_init(&ctx->output, &istream_custom, pool);

    auto *chunked = istream_chunked_new(pool, &ctx->output);
    istream_handler_set(chunked, &MakeIstreamHandler<Custom>::handler, ctx, 0);
    pool_unref(pool);

    istream_read(chunked);
    istream_close(chunked);

    pool_commit();
}

#include "t_istream_filter.hxx"
