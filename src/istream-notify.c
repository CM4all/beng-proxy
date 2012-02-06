/*
 * An istream facade which invokes a callback when the istream is
 * finished / closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-notify.h"
#include "istream-internal.h"

#include <assert.h>

struct istream_notify {
    struct istream output;

    struct istream *input;

    const struct istream_notify_handler *handler;
    void *handler_ctx;
};


/*
 * istream handler
 *
 */

static void
notify_input_eof(void *ctx)
{
    struct istream_notify *notify = ctx;

    notify->handler->eof(notify->handler_ctx);

    istream_deinit_eof(&notify->output);
}

static void
notify_input_abort(GError *error, void *ctx)
{
    struct istream_notify *notify = ctx;

    notify->handler->abort(notify->handler_ctx);

    istream_deinit_abort(&notify->output, error);
}

static const struct istream_handler notify_input_handler = {
    .data = istream_forward_data,
    .direct = istream_forward_direct,
    .eof = notify_input_eof,
    .abort = notify_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_notify *
istream_to_notify(struct istream *istream)
{
    return (struct istream_notify *)(((char*)istream) - offsetof(struct istream_notify, output));
}

static off_t
istream_notify_available(struct istream *istream, bool partial)
{
    struct istream_notify *notify = istream_to_notify(istream);

    return istream_available(notify->input, partial);
}

static void
istream_notify_read(struct istream *istream)
{
    struct istream_notify *notify = istream_to_notify(istream);

    istream_handler_set_direct(notify->input, notify->output.handler_direct);
    istream_read(notify->input);
}

static void
istream_notify_close(struct istream *istream)
{
    struct istream_notify *notify = istream_to_notify(istream);

    notify->handler->close(notify->handler_ctx);

    istream_close_handler(notify->input);
    istream_deinit(&notify->output);
}

static const struct istream_class istream_notify = {
    .available = istream_notify_available,
    .read = istream_notify_read,
    .close = istream_notify_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_notify_new(struct pool *pool, struct istream *input,
                   const struct istream_notify_handler *handler, void *ctx)
{
    struct istream_notify *notify = istream_new_macro(pool, notify);

    assert(input != NULL);
    assert(!istream_has_handler(input));
    assert(handler != NULL);
    assert(handler->eof != NULL);
    assert(handler->abort != NULL);
    assert(handler->close != NULL);

    istream_assign_handler(&notify->input, input,
                           &notify_input_handler, notify,
                           0);

    notify->handler = handler;
    notify->handler_ctx = ctx;

    return istream_struct_cast(&notify->output);
}
