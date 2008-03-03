/*
 * An istream filter which delays the read() and eof() invocations.
 * This is used in the test suite.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <event.h>
#include <string.h>
#include <string.h>

struct istream_later {
    struct istream output;
    istream_t input;
    struct event event;
};


static void
later_event_callback(int fd, short event, void *ctx)
{
    struct istream_later *later = ctx;

    (void)fd;
    (void)event;

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

static size_t
later_source_data(const void *data, size_t length, void *ctx)
{
    struct istream_later *later = ctx;

    return istream_invoke_data(&later->output, data, length);
}

static ssize_t
later_source_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_later *later = ctx;

    return istream_invoke_direct(&later->output, type, fd, max_length);
}

static void
later_source_eof(void *ctx)
{
    struct istream_later *later = ctx;

    istream_clear_unref(&later->input);

    later_schedule(later);
}

static void
later_source_abort(void *ctx)
{
    struct istream_later *later = ctx;

    evtimer_del(&later->event);

    istream_clear_unref(&later->input);
    istream_deinit_abort(&later->output);
}

static const struct istream_handler later_input_handler = {
    .data = later_source_data,
    .direct = later_source_direct,
    .eof = later_source_eof,
    .abort = later_source_abort,
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

    if (later->input == NULL)
        /* this can only happen during the eof callback delay */
        istream_deinit_abort(&later->output);
    else
        istream_close(later->input);
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

    istream_assign_ref_handler(&later->input, input,
                               &later_input_handler, later,
                               0);

    evtimer_set(&later->event, later_event_callback, later);

    return istream_struct_cast(&later->output);
}
