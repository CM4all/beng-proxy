/*
 * This istream filter catches fatal errors and attempts to ignore
 * them.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>

struct istream_catch {
    struct istream output;
    istream_t input;
};


/*
 * istream handler
 *
 */

static size_t
catch_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_catch *catch = ctx;
    return istream_invoke_data(&catch->output, data, length);
}

static ssize_t
catch_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_catch *catch = ctx;
    return istream_invoke_direct(&catch->output, type, fd, max_length);
}

static void
catch_input_abort(void *ctx)
{
    struct istream_catch *catch = ctx;

    catch->input = NULL;
    /* XXX check "available" */
    istream_deinit_eof(&catch->output);
}

static const struct istream_handler catch_input_handler = {
    .data = catch_input_data,
    .direct = catch_input_direct,
    .eof = istream_forward_eof,
    .abort = catch_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_catch *
istream_to_catch(istream_t istream)
{
    return (struct istream_catch *)(((char*)istream) - offsetof(struct istream_catch, output));
}

static void
istream_catch_read(istream_t istream)
{
    struct istream_catch *catch = istream_to_catch(istream);

    istream_handler_set_direct(catch->input, catch->output.handler_direct);
    istream_read(catch->input);
}

static void
istream_catch_close(istream_t istream)
{
    struct istream_catch *catch = istream_to_catch(istream);

    assert(catch->input != NULL);

    istream_free_handler(&catch->input);
    istream_deinit_abort(&catch->output);
}

static const struct istream istream_catch = {
    .read = istream_catch_read,
    .close = istream_catch_close,
};


/*
 * constructor
 *
 */

istream_t
istream_catch_new(pool_t pool, istream_t input)
{
    struct istream_catch *catch = istream_new_macro(pool, catch);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    istream_assign_handler(&catch->input, input,
                           &catch_input_handler, catch,
                           0);

    return istream_struct_cast(&catch->output);
}
