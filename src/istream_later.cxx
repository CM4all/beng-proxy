/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_later.hxx"
#include "istream_internal.hxx"
#include "istream_forward.hxx"
#include "event/defer_event.h"
#include "util/Cast.hxx"
#include "pool.hxx"

#include <event.h>

struct LaterIstream {
    struct istream output;
    struct istream *input;
    struct defer_event defer_event;
};


static void
later_event_callback(int fd gcc_unused, short event gcc_unused,
                     void *ctx)
{
    auto *later = (LaterIstream *)ctx;

    if (later->input == nullptr)
        istream_deinit_eof(&later->output);
    else
        istream_read(later->input);
}

static void later_schedule(LaterIstream *later)
{
    defer_event_add(&later->defer_event);
}


/*
 * istream handler
 *
 */

static void
later_input_eof(void *ctx)
{
    auto *later = (LaterIstream *)ctx;

    later->input = nullptr;

    later_schedule(later);
}

static void
later_input_abort(GError *error, void *ctx)
{
    auto *later = (LaterIstream *)ctx;

    defer_event_deinit(&later->defer_event);

    later->input = nullptr;
    istream_deinit_abort(&later->output, error);
}

static constexpr struct istream_handler later_input_handler = {
    .data = istream_forward_data,
    .direct = istream_forward_direct,
    .eof = later_input_eof,
    .abort = later_input_abort,
};


/*
 * istream implementation
 *
 */

static inline LaterIstream *
istream_to_later(struct istream *istream)
{
    return &ContainerCast2(*istream, &LaterIstream::output);
}

static void
istream_later_read(struct istream *istream)
{
    LaterIstream *later = istream_to_later(istream);

    later_schedule(later);
}

static void
istream_later_close(struct istream *istream)
{
    LaterIstream *later = istream_to_later(istream);

    defer_event_deinit(&later->defer_event);

    /* input can only be nullptr during the eof callback delay */
    if (later->input != nullptr)
        istream_close_handler(later->input);

    istream_deinit(&later->output);
}

static constexpr struct istream_class istream_later = {
    .read = istream_later_read,
    .close = istream_later_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_later_new(struct pool *pool, struct istream *input)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto later = NewFromPool<LaterIstream>(*pool);
    istream_init(&later->output, &istream_later, pool);

    istream_assign_handler(&later->input, input,
                           &later_input_handler, later,
                           0);

    defer_event_init(&later->defer_event, later_event_callback, later);

    return &later->output;
}
