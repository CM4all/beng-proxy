/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_stopwatch.hxx"
#include "istream-internal.h"
#include "stopwatch.h"
#include "util/Cast.hxx"

#include <assert.h>

struct istream_stopwatch {
    struct istream output;

    struct istream *input;

    struct stopwatch *stopwatch;
};


/*
 * istream handler
 *
 */

static void
stopwatch_input_eof(void *ctx)
{
    auto *stopwatch = (struct istream_stopwatch *)ctx;

    stopwatch_event(stopwatch->stopwatch, "end");
    stopwatch_dump(stopwatch->stopwatch);

    istream_deinit_eof(&stopwatch->output);
}

static void
stopwatch_input_abort(GError *error, void *ctx)
{
    auto *stopwatch = (struct istream_stopwatch *)ctx;

    stopwatch_event(stopwatch->stopwatch, "abort");
    stopwatch_dump(stopwatch->stopwatch);

    istream_deinit_abort(&stopwatch->output, error);
}

static constexpr struct istream_handler stopwatch_input_handler = {
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
istream_to_stopwatch(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_stopwatch::output);
}

static void
istream_stopwatch_read(struct istream *istream)
{
    struct istream_stopwatch *stopwatch = istream_to_stopwatch(istream);

    istream_handler_set_direct(stopwatch->input,
                               stopwatch->output.handler_direct);

    istream_read(stopwatch->input);
}

static int
istream_stopwatch_as_fd(struct istream *istream)
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
istream_stopwatch_close(struct istream *istream)
{
    struct istream_stopwatch *stopwatch = istream_to_stopwatch(istream);

    assert(stopwatch->input != nullptr);

    istream_close_handler(stopwatch->input);
    istream_deinit(&stopwatch->output);
}

static constexpr struct istream_class istream_stopwatch = {
    .read = istream_stopwatch_read,
    .as_fd = istream_stopwatch_as_fd,
    .close = istream_stopwatch_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_stopwatch_new(struct pool *pool, struct istream *input,
                      struct stopwatch *_stopwatch)
{
    struct istream_stopwatch *stopwatch;

    assert(input != nullptr);
    assert(!istream_has_handler(input));

    if (_stopwatch == nullptr)
        return input;

    stopwatch = istream_new_macro(pool, stopwatch);

    istream_assign_handler(&stopwatch->input, input,
                           &stopwatch_input_handler, stopwatch,
                           0);

    stopwatch->stopwatch = _stopwatch;

    return istream_struct_cast(&stopwatch->output);
}
