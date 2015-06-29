/*
 * An istream facade which invokes a callback when the istream is
 * finished / closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_notify.hxx"
#include "istream_forward.hxx"
#include "util/Cast.hxx"

#include <assert.h>

class NotifyIstream final : public ForwardIstream {
    const struct istream_notify_handler &handler;
    void *const handler_ctx;

public:
    NotifyIstream(struct pool &p, struct istream &_input,
                  const struct istream_notify_handler &_handler, void *_ctx)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<NotifyIstream>::handler, this),
         handler(_handler), handler_ctx(_ctx) {}

    /* virtual methods from class Istream */

    void Close() override {
        handler.close(handler_ctx);
        ForwardIstream::Close();
    }

    /* handler */

    void OnEof() {
        handler.eof(handler_ctx);
        ForwardIstream::OnEof();
    }

    void OnError(GError *error) {
        handler.abort(handler_ctx);
        ForwardIstream::OnError(error);
    }
};

/*
 * constructor
 *
 */

struct istream *
istream_notify_new(struct pool &pool, struct istream &input,
                   const struct istream_notify_handler &handler, void *ctx)
{
    assert(!istream_has_handler(&input));
    assert(handler.eof != nullptr);
    assert(handler.abort != nullptr);
    assert(handler.close != nullptr);

    return NewIstream<NotifyIstream>(pool, input, handler, ctx);
}
