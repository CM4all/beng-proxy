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

struct Custom final : Istream {
    bool eof;
    GError *error;

    explicit Custom(struct pool &p):Istream(p) {}

    /* virtual methods from class Istream */

    off_t GetAvailable(gcc_unused bool partial) override {
        return 1;
    }

    void Read() override {}

    /* istream handler */
    size_t OnData(gcc_unused const void *data, gcc_unused size_t length) {
        InvokeData(" ", 1);
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

static void
test_custom(struct pool *pool)
{
    pool = pool_new_linear(pool, "test", 8192);
    auto *ctx = NewFromPool<Custom>(*pool, *pool);

    auto *chunked = istream_chunked_new(pool, ctx->Cast());
    istream_handler_set(chunked, &MakeIstreamHandler<Custom>::handler, ctx, 0);
    pool_unref(pool);

    istream_read(chunked);
    istream_close(chunked);

    pool_commit();
}

#include "t_istream_filter.hxx"
