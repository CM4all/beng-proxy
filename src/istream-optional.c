/*
 * An istream facade which holds an optional istream.  It blocks until
 * it is told to resume or to discard the inner istream.  Errors are
 * reported to the handler immediately.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "istream-forward.h"

#include <assert.h>

struct istream_optional {
    struct istream output;

    istream_t input;

    bool resumed;
};


/*
 * istream handler
 *
 */

static size_t
optional_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_optional *optional = ctx;

    return optional->resumed
        ? istream_invoke_data(&optional->output, data, length)
        : 0;
}

static ssize_t
optional_input_direct(istream_direct_t type, int fd, size_t max_length,
                      void *ctx)
{
    struct istream_optional *optional = ctx;

    assert(optional->resumed);

    return istream_invoke_direct(&optional->output, type, fd, max_length);
}

static const struct istream_handler optional_input_handler = {
    .data = optional_input_data,
    .direct = optional_input_direct,
    .eof = istream_forward_eof,
    .abort = istream_forward_abort
};


/*
 * istream implementation
 *
 */

static inline struct istream_optional *
istream_to_optional(istream_t istream)
{
    return (struct istream_optional *)(((char*)istream) - offsetof(struct istream_optional, output));
}

static off_t
istream_optional_available(istream_t istream, bool partial)
{
    struct istream_optional *optional = istream_to_optional(istream);

    return optional->resumed
        ? istream_available(optional->input, partial)
        : -1;
}

static void
istream_optional_read(istream_t istream)
{
    struct istream_optional *optional = istream_to_optional(istream);

    if (optional->resumed) {
        istream_handler_set_direct(optional->input,
                                   optional->output.handler_direct);
        istream_read(optional->input);
    }
}

static int
istream_optional_as_fd(istream_t istream)
{
    struct istream_optional *optional = istream_to_optional(istream);

    return optional->resumed
        ? istream_as_fd(optional->input)
        : -1;
}

static void
istream_optional_close(istream_t istream)
{
    struct istream_optional *optional = istream_to_optional(istream);

    istream_close_handler(optional->input);
    istream_deinit_abort(&optional->output);
}

static const struct istream istream_optional = {
    .available = istream_optional_available,
    .read = istream_optional_read,
    .as_fd = istream_optional_as_fd,
    .close = istream_optional_close,
};


/*
 * constructor
 *
 */

istream_t
istream_optional_new(pool_t pool, istream_t input)
{
    struct istream_optional *optional = istream_new_macro(pool, optional);

    istream_assign_handler(&optional->input, input,
                           &optional_input_handler, optional,
                           0);

    optional->resumed = false;

    return istream_struct_cast(&optional->output);
}

void
istream_optional_resume(istream_t istream)
{
    struct istream_optional *optional = (struct istream_optional *)istream;

    assert(!optional->resumed);

    optional->resumed = true;
}

void
istream_optional_discard(istream_t istream)
{
    struct istream_optional *optional = (struct istream_optional *)istream;

    assert(!optional->resumed);

    optional->resumed = true;

    /* replace the input with a "null" istream */
    istream_close_handler(optional->input);
    istream_assign_handler(&optional->input,
                           istream_null_new(optional->output.pool),
                           &optional_input_handler, optional,
                           optional->output.handler_direct);
}
