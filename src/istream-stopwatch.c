/*
 * This istream filter emits a stopwatch event and dump on eof/abort.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "stopwatch.h"

#include <assert.h>

struct istream_stopwatch {
    struct istream output;

    istream_t input;

    struct stopwatch *stopwatch;
};


/*
 * istream handler
 *
 */

static void
stopwatch_input_eof(void *ctx)
{
    struct istream_stopwatch *stopwatch = ctx;

    stopwatch_event(stopwatch->stopwatch, "end");
    stopwatch_dump(stopwatch->stopwatch);

    istream_deinit_eof(&stopwatch->output);
}

static void
stopwatch_input_abort(void *ctx)
{
    struct istream_stopwatch *stopwatch = ctx;

    stopwatch_event(stopwatch->stopwatch, "abort");
    stopwatch_dump(stopwatch->stopwatch);

    istream_deinit_abort(&stopwatch->output);
}

static const struct istream_handler stopwatch_input_handler = {
    .data = istream_forward_data,
    .direct = istream_forward_direct,
    .eof = stopwatch_input_eof,
    .abort = stopwatch_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_stopwatch *
istream_to_stopwatch(istream_t istream)
{
    return (struct istream_stopwatch *)(((char*)istream) - offsetof(struct istream_stopwatch, output));
}

static void
istream_stopwatch_read(istream_t istream)
{
    struct istream_stopwatch *stopwatch = istream_to_stopwatch(istream);

    istream_handler_set_direct(stopwatch->input,
                               stopwatch->output.handler_direct);

    istream_read(stopwatch->input);
}

static int
istream_stopwatch_as_fd(istream_t istream)
{
    struct istream_stopwatch *stopwatch = istream_to_stopwatch(istream);

    int fd = istream_as_fd(stopwatch->input);
    if (fd >= 0) {
        stopwatch_event(stopwatch->stopwatch, "as_fd");
        stopwatch_dump(stopwatch->stopwatch);
        istream_deinit(&stopwatch->output);
    }

    return fd;
}

static void
istream_stopwatch_close(istream_t istream)
{
    struct istream_stopwatch *stopwatch = istream_to_stopwatch(istream);

    assert(stopwatch->input != NULL);

    istream_close_handler(stopwatch->input);
    istream_deinit_abort(&stopwatch->output);
}

static const struct istream istream_stopwatch = {
    .read = istream_stopwatch_read,
    .as_fd = istream_stopwatch_as_fd,
    .close = istream_stopwatch_close,
};


/*
 * constructor
 *
 */

istream_t
istream_stopwatch_new(pool_t pool, istream_t input,
                      struct stopwatch *_stopwatch)
{
    struct istream_stopwatch *stopwatch;

    assert(input != NULL);
    assert(!istream_has_handler(input));

    if (_stopwatch == NULL)
        return input;

    stopwatch = istream_new_macro(pool, stopwatch);

    istream_assign_handler(&stopwatch->input, input,
                           &stopwatch_input_handler, stopwatch,
                           0);

    stopwatch->stopwatch = _stopwatch;

    return istream_struct_cast(&stopwatch->output);
}
