/*
 * An istream facade which invokes a callback when the istream is
 * finished / closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_notify.hxx"
#include "ForwardIstream.hxx"

#include <assert.h>

class NotifyIstream final : public ForwardIstream {
    const struct istream_notify_handler &handler;
    void *const handler_ctx;

public:
    NotifyIstream(struct pool &p, Istream &_input,
                  const struct istream_notify_handler &_handler, void *_ctx)
        :ForwardIstream(p, _input),
         handler(_handler), handler_ctx(_ctx) {}

    /* virtual methods from class Istream */

    void _Close() override {
        handler.close(handler_ctx);
        ForwardIstream::_Close();
    }

    /* virtual methods from class IstreamHandler */

    void OnEof() override {
        handler.eof(handler_ctx);
        ForwardIstream::OnEof();
    }

    void OnError(std::exception_ptr ep) override {
        handler.abort(handler_ctx);
        ForwardIstream::OnError(ep);
    }
};

/*
 * constructor
 *
 */

Istream *
istream_notify_new(struct pool &pool, Istream &input,
                   const struct istream_notify_handler &handler, void *ctx)
{
    assert(handler.eof != nullptr);
    assert(handler.abort != nullptr);
    assert(handler.close != nullptr);

    return NewIstream<NotifyIstream>(pool, input, handler, ctx);
}
