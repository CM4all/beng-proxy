#include "istream/istream_chunked.hxx"
#include "istream/istream_string.hxx"
#include "istream/istream_oo.hxx"
#include "istream/istream.hxx"
#include "pool.hxx"

static Istream *
create_input(struct pool *pool)
{
    return istream_string_new(pool, "foo_bar_0123456789abcdefghijklmnopqrstuvwxyz");
}

static Istream *
create_test(struct pool *pool, Istream *input)
{
    return istream_chunked_new(*pool, *input);
}

#define CUSTOM_TEST

struct Custom final : Istream, IstreamHandler {
    bool eof;
    GError *error;

    explicit Custom(struct pool &p):Istream(p) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) override {
        return 1;
    }

    void _Read() override {}

    /* virtual methods from class IstreamHandler */

    size_t OnData(gcc_unused const void *data,
                  gcc_unused size_t length) override {
        InvokeData(" ", 1);
        return 0;
    }

    void OnEof() override {
        eof = true;
    }

    void OnError(GError *_error) override {
        error = _error;
    }
};

static void
test_custom(struct pool *pool)
{
    pool = pool_new_linear(pool, "test", 8192);
    auto *ctx = NewFromPool<Custom>(*pool, *pool);

    auto *chunked = istream_chunked_new(*pool, *ctx);
    chunked->SetHandler(*ctx);
    pool_unref(pool);

    chunked->Read();
    chunked->Close();

    pool_commit();
}

#include "t_istream_filter.hxx"
