/*
 * An istream facade which waits for the istream handler to appear.
 * Until then, it blocks all read requests from the inner stream.
 *
 * This class is required because all other istreams require a handler
 * to be installed.  In the case of HTTP proxying, the request body
 * istream has no handler until the connection to the other HTTP
 * server is open.  Meanwhile, istream_hold blocks all read requests
 * from the client's request body.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>

struct istream_hold {
    struct istream output;
    istream_t input;
    bool input_eof, input_aborted;
};


/*
 * istream handler
 *
 */

static size_t
hold_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_hold *hold = ctx;

    if (hold->output.handler == NULL)
        return 0;

    return istream_invoke_data(&hold->output, data, length);
}

static ssize_t
hold_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_hold *hold = ctx;

    if (hold->output.handler == NULL)
        return -2;

    return istream_invoke_direct(&hold->output, type, fd, max_length);
}

static void
hold_input_eof(void *ctx)
{
    struct istream_hold *hold = ctx;

    assert(!hold->input_eof);
    assert(!hold->input_aborted);

    if (hold->output.handler == NULL) {
        /* queue the eof() call */
        hold->input_eof = true;
        return;
    }

    istream_deinit_eof(&hold->output);
}

static void
hold_input_abort(void *ctx)
{
    struct istream_hold *hold = ctx;

    assert(!hold->input_eof);
    assert(!hold->input_aborted);

    if (hold->output.handler == NULL) {
        /* queue the abort() call */
        hold->input_aborted = true;
        return;
    }

    istream_deinit_abort(&hold->output);
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
istream_to_hold(istream_t istream)
{
    return (struct istream_hold *)(((char*)istream) - offsetof(struct istream_hold, output));
}

static off_t
istream_hold_available(istream_t istream, bool partial)
{
    struct istream_hold *hold = istream_to_hold(istream);

    if (unlikely(hold->input_eof))
        return 0;
    else if (unlikely(hold->input_aborted))
        return (off_t)-1;

    return istream_available(hold->input, partial);
}

static void
istream_hold_read(istream_t istream)
{
    struct istream_hold *hold = istream_to_hold(istream);

    assert(hold->output.handler != NULL);

    if (unlikely(hold->input_eof))
        istream_deinit_eof(&hold->output);
    else if (unlikely(hold->input_aborted))
        istream_deinit_abort(&hold->output);
    else {
        istream_handler_set_direct(hold->input, hold->output.handler_direct);
        istream_read(hold->input);
    }
}

static void
istream_hold_close(istream_t istream)
{
    struct istream_hold *hold = istream_to_hold(istream);

    if (hold->input_eof || hold->input_aborted)
        istream_deinit_abort(&hold->output);
    else if (hold->output.handler == NULL) {
        /* there is no handler yet - immediately deinitialize this
           istream */
        istream_close_handler(hold->input);
        istream_deinit(&hold->output);
    } else
        /* the input object is still there; istream_close(hold->input)
           will implicitly call istream_invoke_free(&hold->output)
           through hold_input_free() */
        istream_close(hold->input);
}

static const struct istream istream_hold = {
    .available = istream_hold_available,
    .read = istream_hold_read,
    .close = istream_hold_close,
};


/*
 * constructor
 *
 */

istream_t
istream_hold_new(pool_t pool, istream_t input)
{
    struct istream_hold *hold = istream_new_macro(pool, hold);

    hold->input_eof = false;
    hold->input_aborted = false;

    istream_assign_handler(&hold->input, input,
                           &hold_input_handler, hold,
                           0);

    return istream_struct_cast(&hold->output);
}
