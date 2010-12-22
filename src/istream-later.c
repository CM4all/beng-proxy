/*
 * An istream filter which delays the read() and eof() invocations.
 * This is used in the test suite.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "istream-forward.h"

#include <event.h>

struct istream_later {
    struct istream output;
    istream_t input;
    struct event event;
};


static void
later_event_callback(int fd __attr_unused, short event __attr_unused,
                     void *ctx)
{
    struct istream_later *later = ctx;

    if (later->input == NULL)
        istream_deinit_eof(&later->output);
    else
        istream_read(later->input);
}

static void later_schedule(struct istream_later *later)
{
    struct timeval tv = { .tv_sec = 0, .tv_usec = 0 };

    if (evtimer_pending(&later->event, NULL) == 0)
        evtimer_add(&later->event, &tv);
}


/*
 * istream handler
 *
 */

static void
later_input_eof(void *ctx)
{
    struct istream_later *later = ctx;

    later->input = NULL;

    later_schedule(later);
}

static void
later_input_abort(GError *error, void *ctx)
{
    struct istream_later *later = ctx;

    evtimer_del(&later->event);

    later->input = NULL;
    istream_deinit_abort(&later->output, error);
}

static const struct istream_handler later_input_handler = {
    .data = istream_forward_data,
    .direct = istream_forward_direct,
    .eof = later_input_eof,
    .abort = later_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_later *
istream_to_later(istream_t istream)
{
    return (struct istream_later *)(((char*)istream) - offsetof(struct istream_later, output));
}

static void
istream_later_read(istream_t istream)
{
    struct istream_later *later = istream_to_later(istream);

    later_schedule(later);
}

static void
istream_later_close(istream_t istream)
{
    struct istream_later *later = istream_to_later(istream);

    evtimer_del(&later->event);

    /* input can only be NULL during the eof callback delay */
    if (later->input != NULL)
        istream_close_handler(later->input);

    istream_deinit(&later->output);
}

static const struct istream istream_later = {
    .read = istream_later_read,
    .close = istream_later_close,
};


/*
 * constructor
 *
 */

istream_t
istream_later_new(pool_t pool, istream_t input)
{
    struct istream_later *later = istream_new_macro(pool, later);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_assign_handler(&later->input, input,
                           &later_input_handler, later,
                           0);

    evtimer_set(&later->event, later_event_callback, later);

    return istream_struct_cast(&later->output);
}
