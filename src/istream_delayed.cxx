/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_delayed.hxx"
#include "istream-internal.h"
#include "async.hxx"
#include "util/Cast.hxx"

#include <glib.h>

#include <assert.h>
#include <string.h>

struct istream_delayed {
    struct istream output;
    struct istream *input;
    struct async_operation_ref async;
};


/*
 * istream implementation
 *
 */

static inline struct istream_delayed *
istream_to_delayed(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_delayed::output);
}

static off_t
istream_delayed_available(struct istream *istream, bool partial)
{
    struct istream_delayed *delayed = istream_to_delayed(istream);

    if (delayed->input == nullptr)
        return (off_t)-1;
    else
        return istream_available(delayed->input, partial);
}

static void
istream_delayed_read(struct istream *istream)
{
    struct istream_delayed *delayed = istream_to_delayed(istream);

    if (delayed->input != nullptr) {
        istream_handler_set_direct(delayed->input,
                                   delayed->output.handler_direct);
        istream_read(delayed->input);
    }
}

static int
istream_delayed_as_fd(struct istream *istream)
{
    struct istream_delayed *delayed = istream_to_delayed(istream);

    if (delayed->input == nullptr)
        return -1;

    int fd = istream_as_fd(delayed->input);
    if (fd >= 0)
        istream_deinit(&delayed->output);

    return fd;
}

static void
istream_delayed_close(struct istream *istream)
{
    struct istream_delayed *delayed = istream_to_delayed(istream);

    if (delayed->input != nullptr)
        istream_close_handler(delayed->input);
    else if (delayed->async.IsDefined())
        delayed->async.Abort();

    istream_deinit(&delayed->output);
}

static const struct istream_class istream_delayed = {
    .available = istream_delayed_available,
    .read = istream_delayed_read,
    .as_fd = istream_delayed_as_fd,
    .close = istream_delayed_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_delayed_new(struct pool *pool)
{
    struct istream_delayed *delayed = istream_new_macro(pool, delayed);

    delayed->input = nullptr;
    return istream_struct_cast(&delayed->output);
}

struct async_operation_ref *
istream_delayed_async_ref(struct istream *i_delayed)
{
    struct istream_delayed *delayed = (struct istream_delayed *)i_delayed;

    return &delayed->async;
}

void
istream_delayed_set(struct istream *i_delayed, struct istream *input)
{
    struct istream_delayed *delayed = (struct istream_delayed *)i_delayed;

    assert(delayed != nullptr);
    assert(delayed->input == nullptr);
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    delayed->async.Poison();

    istream_assign_handler(&delayed->input, input,
                           &istream_forward_handler, &delayed->output,
                           delayed->output.handler_direct);
}

void
istream_delayed_set_eof(struct istream *i_delayed)
{
    struct istream_delayed *delayed = (struct istream_delayed *)i_delayed;

    assert(delayed != nullptr);
    assert(delayed->input == nullptr);

    delayed->async.Poison();

    istream_deinit_eof(&delayed->output);
}

void
istream_delayed_set_abort(struct istream *i_delayed, GError *error)
{
    struct istream_delayed *delayed = (struct istream_delayed *)i_delayed;

    assert(delayed != nullptr);
    assert(delayed->input == nullptr);

    delayed->async.Poison();

    istream_deinit_abort(&delayed->output, error);
}
