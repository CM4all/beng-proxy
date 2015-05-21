/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_hold.hxx"
#include "istream-internal.h"
#include "util/Cast.hxx"

#include <assert.h>

struct istream_hold {
    struct istream output;
    struct istream *input;
    bool input_eof;

    GError *input_error;
};


/*
 * istream handler
 *
 */

static size_t
hold_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_hold *hold = (struct istream_hold *)ctx;

    if (hold->output.handler == nullptr)
        return 0;

    return istream_invoke_data(&hold->output, data, length);
}

static ssize_t
hold_input_direct(enum istream_direct type, int fd, size_t max_length,
                  void *ctx)
{
    struct istream_hold *hold = (struct istream_hold *)ctx;

    if (hold->output.handler == nullptr)
        return ISTREAM_RESULT_BLOCKING;

    return istream_invoke_direct(&hold->output, type, fd, max_length);
}

static void
hold_input_eof(void *ctx)
{
    struct istream_hold *hold = (struct istream_hold *)ctx;

    assert(!hold->input_eof);
    assert(hold->input_error == nullptr);

    if (hold->output.handler == nullptr) {
        /* queue the eof() call */
        hold->input_eof = true;
        return;
    }

    istream_deinit_eof(&hold->output);
}

static void
hold_input_abort(GError *error, void *ctx)
{
    struct istream_hold *hold = (struct istream_hold *)ctx;

    assert(!hold->input_eof);
    assert(hold->input_error == nullptr);

    if (hold->output.handler == nullptr) {
        /* queue the abort() call */
        hold->input_error = error;
        return;
    }

    istream_deinit_abort(&hold->output, error);
}

static const struct istream_handler hold_input_handler = {
    .data = hold_input_data,
    .direct = hold_input_direct,
    .eof = hold_input_eof,
    .abort = hold_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_hold *
istream_to_hold(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_hold::output);
}

static off_t
istream_hold_available(struct istream *istream, bool partial)
{
    struct istream_hold *hold = istream_to_hold(istream);

    if (unlikely(hold->input_eof))
        return 0;
    else if (unlikely(hold->input_error != nullptr))
        return (off_t)-1;

    return istream_available(hold->input, partial);
}

static void
istream_hold_read(struct istream *istream)
{
    struct istream_hold *hold = istream_to_hold(istream);

    assert(hold->output.handler != nullptr);

    if (unlikely(hold->input_eof))
        istream_deinit_eof(&hold->output);
    else if (unlikely(hold->input_error != nullptr))
        istream_deinit_abort(&hold->output, hold->input_error);
    else {
        istream_handler_set_direct(hold->input, hold->output.handler_direct);
        istream_read(hold->input);
    }
}

static int
istream_hold_as_fd(struct istream *istream)
{
    struct istream_hold *hold = istream_to_hold(istream);

    int fd = istream_as_fd(hold->input);
    if (fd >= 0)
        istream_deinit(&hold->output);

    return fd;
}

static void
istream_hold_close(struct istream *istream)
{
    struct istream_hold *hold = istream_to_hold(istream);

    if (hold->input_eof)
        istream_deinit(&hold->output);
    else if (hold->input_error != nullptr) {
        /* the handler is not interested in the error */
        g_error_free(hold->input_error);
        istream_deinit(&hold->output);
    } else {
        /* the input object is still there */
        istream_close_handler(hold->input);
        istream_deinit(&hold->output);
    }
}

static const struct istream_class istream_hold = {
    .available = istream_hold_available,
    .read = istream_hold_read,
    .as_fd = istream_hold_as_fd,
    .close = istream_hold_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_hold_new(struct pool *pool, struct istream *input)
{
    struct istream_hold *hold = istream_new_macro(pool, hold);

    hold->input_eof = false;
    hold->input_error = nullptr;

    istream_assign_handler(&hold->input, input,
                           &hold_input_handler, hold,
                           0);

    return hold->output;
}
