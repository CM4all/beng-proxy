/*
 * An istream facade which invokes a callback when the istream is
 * finished / closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_notify.hxx"
#include "istream-internal.h"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <assert.h>

struct NotifyIstream {
    struct istream output;

    struct istream *input;

    const struct istream_notify_handler &handler;
    void *const handler_ctx;

    NotifyIstream(struct pool &p, struct istream &_input,
                  const struct istream_notify_handler &_handler, void *_ctx);
};


/*
 * istream handler
 *
 */

static void
notify_input_eof(void *ctx)
{
    NotifyIstream &notify = *(NotifyIstream *)ctx;

    notify.handler.eof(notify.handler_ctx);

    istream_deinit_eof(&notify.output);
}

static void
notify_input_abort(GError *error, void *ctx)
{
    NotifyIstream &notify = *(NotifyIstream *)ctx;

    notify.handler.abort(notify.handler_ctx);

    istream_deinit_abort(&notify.output, error);
}

static constexpr struct istream_handler notify_input_handler = {
    .data = istream_forward_data,
    .direct = istream_forward_direct,
    .eof = notify_input_eof,
    .abort = notify_input_abort,
};


/*
 * istream implementation
 *
 */

static inline NotifyIstream &
istream_to_notify(struct istream *istream)
{
    return ContainerCast2(*istream, &NotifyIstream::output);
}

static off_t
istream_notify_available(struct istream *istream, bool partial)
{
    NotifyIstream &notify = istream_to_notify(istream);

    return istream_available(notify.input, partial);
}

static void
istream_notify_read(struct istream *istream)
{
    NotifyIstream &notify = istream_to_notify(istream);

    istream_handler_set_direct(notify.input, notify.output.handler_direct);
    istream_read(notify.input);
}

static void
istream_notify_close(struct istream *istream)
{
    NotifyIstream &notify = istream_to_notify(istream);

    notify.handler.close(notify.handler_ctx);

    istream_close_handler(notify.input);
    istream_deinit(&notify.output);
}

static constexpr struct istream_class istream_notify = {
    .available = istream_notify_available,
    .read = istream_notify_read,
    .close = istream_notify_close,
};


/*
 * constructor
 *
 */

inline NotifyIstream::NotifyIstream(struct pool &p, struct istream &_input,
                                    const struct istream_notify_handler &_handler, void *_ctx)
    :handler(_handler), handler_ctx(_ctx)
{
    istream_init(&output, &istream_notify, &p);

    istream_assign_handler(&input, &_input,
                           &notify_input_handler, this,
                           0);
}

struct istream *
istream_notify_new(struct pool &pool, struct istream &input,
                   const struct istream_notify_handler &handler, void *ctx)
{
    assert(!istream_has_handler(&input));
    assert(handler.eof != nullptr);
    assert(handler.abort != nullptr);
    assert(handler.close != nullptr);

    auto *notify = NewFromPool<NotifyIstream>(pool, pool, input, handler, ctx);
    return &notify->output;
}
