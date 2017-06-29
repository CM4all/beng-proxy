/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "StringSink.hxx"
#include "Sink.hxx"
#include "pool.hxx"
#include "util/Cancellable.hxx"

struct StringSink final : IstreamSink, Cancellable {
    struct pool &pool;

    std::string value;

    void (*callback)(std::string &&value, std::exception_ptr error, void *ctx);
    void *callback_ctx;

    StringSink(struct pool &_pool, Istream &_input,
                void (*_callback)(std::string &&value, std::exception_ptr error,
                                  void *ctx),
                void *_ctx,
                CancellablePointer &cancel_ptr)
        :IstreamSink(_input, FD_ANY), pool(_pool),
         callback(_callback), callback_ctx(_ctx) {
        cancel_ptr = *this;
    }

    void Destroy() {
        this->~StringSink();
    }

    /* virtual methods from class Cancellable */
    void Cancel() override {
        const ScopePoolRef ref(pool TRACE_ARGS);
        input.Close();
        Destroy();
    }

    /* virtual methods from class IstreamHandler */

    size_t OnData(const void *data, size_t length) override {
        value.append((const char *)data, length);
        return length;
    }

    void OnEof() override {
        callback(std::move(value), nullptr, callback_ctx);
        Destroy();
    }

    void OnError(std::exception_ptr ep) override {
        callback(std::move(value), ep, callback_ctx);
        Destroy();
    }
};

/*
 * constructor
 *
 */

void
NewStringSink(struct pool &pool, Istream &input,
              void (*callback)(std::string &&value, std::exception_ptr error,
                               void *ctx),
              void *ctx, CancellablePointer &cancel_ptr)
{
    NewFromPool<StringSink>(pool, pool, input,
                            callback, ctx, cancel_ptr);
}
