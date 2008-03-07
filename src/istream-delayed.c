/*
 * An istream facade which waits for its inner istream to appear.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "async.h"

#include <assert.h>
#include <string.h>

struct istream_delayed {
    struct istream output;
    istream_t input;
    struct async_operation_ref async;
};


/*
 * istream handler
 *
 */

static size_t
delayed_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_delayed *delayed = ctx;

    return istream_invoke_data(&delayed->output, data, length);
}

static ssize_t
delayed_input_direct(istream_direct_t type, int fd, size_t max_length, void *ctx)
{
    struct istream_delayed *delayed = ctx;

    return istream_invoke_direct(&delayed->output, type, fd, max_length);
}

static const struct istream_handler delayed_input_handler = {
    .data = delayed_input_data,
    .direct = delayed_input_direct,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_delayed *
istream_to_delayed(istream_t istream)
{
    return (struct istream_delayed *)(((char*)istream) - offsetof(struct istream_delayed, output));
}

static off_t
istream_delayed_available(istream_t istream, int partial)
{
    struct istream_delayed *delayed = istream_to_delayed(istream);

    if (delayed->input == NULL)
        return (off_t)-1;
    else
        return istream_available(delayed->input, partial);
}

static void
istream_delayed_read(istream_t istream)
{
    struct istream_delayed *delayed = istream_to_delayed(istream);

    if (delayed->input != NULL)
        istream_read(delayed->input);
}

static void
istream_delayed_close(istream_t istream)
{
    struct istream_delayed *delayed = istream_to_delayed(istream);

    if (delayed->input != NULL)
        istream_close(delayed->input);
    else if (async_ref_defined(&delayed->async)) {
        async_abort(&delayed->async);
        istream_deinit_abort(&delayed->output);
    }
}

static const struct istream istream_delayed = {
    .available = istream_delayed_available,
    .read = istream_delayed_read,
    .close = istream_delayed_close,
};


/*
 * constructor
 *
 */

istream_t
istream_delayed_new(pool_t pool, struct async_operation *async)
{
    struct istream_delayed *delayed = istream_new_macro(pool, delayed);

    delayed->input = NULL;

    if (async == NULL)
        async_ref_clear(&delayed->async);
    else
        async_ref_set(&delayed->async, async);

    return istream_struct_cast(&delayed->output);
}

void
istream_delayed_set(istream_t i_delayed, istream_t input)
{
    struct istream_delayed *delayed = (struct istream_delayed *)i_delayed;

    assert(delayed != NULL);
    assert(delayed->input == NULL);
    assert(input != NULL);
    assert(!istream_has_handler(input));

    async_ref_poison(&delayed->async);

    istream_assign_handler(&delayed->input, input,
                           &delayed_input_handler, delayed,
                           delayed->output.handler_direct);
}

void
istream_delayed_set_eof(istream_t i_delayed)
{
    struct istream_delayed *delayed = (struct istream_delayed *)i_delayed;

    assert(delayed != NULL);
    assert(delayed->input == NULL);

    async_ref_poison(&delayed->async);

    istream_deinit_eof(&delayed->output);
}
