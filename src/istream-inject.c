/*
 * Fault injection istream filter.  This istream forwards data from
 * its input, but will never forward eof/abort.  The "abort" can be
 * injected at any time.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <assert.h>

struct istream_inject {
    struct istream output;
    istream_t input;
};


/*
 * istream handler
 *
 */

static size_t
inject_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_inject *inject = ctx;

    return istream_invoke_data(&inject->output, data, length);
}

static ssize_t
inject_input_direct(istream_direct_t type, int fd, size_t max_length,
                    void *ctx)
{
    struct istream_inject *inject = ctx;

    return istream_invoke_direct(&inject->output, type, fd, max_length);
}

static void
inject_input_end(void *ctx)
{
    struct istream_inject *inject = ctx;

    assert(inject->input != NULL);

    inject->input = NULL;
}

static const struct istream_handler inject_input_handler = {
    .data = inject_input_data,
    .direct = inject_input_direct,
    .eof = inject_input_end,
    .abort = inject_input_end,
};


/*
 * istream implementation
 *
 */

static inline struct istream_inject *
istream_to_inject(istream_t istream)
{
    return (struct istream_inject *)(((char*)istream) - offsetof(struct istream_inject, output));
}

static off_t
istream_inject_available(istream_t istream, bool partial)
{
    struct istream_inject *inject = istream_to_inject(istream);

    /* never return the total length, because the caller may then make
       assumptions on when this stream ends */

    return partial && inject->input != NULL
        ? istream_available(inject->input, partial)
        : -1;
}

static void
istream_inject_read(istream_t istream)
{
    struct istream_inject *inject = istream_to_inject(istream);

    if (inject->input == NULL)
        return;

    istream_handler_set_direct(inject->input, inject->output.handler_direct);
    istream_read(inject->input);
}

static void
istream_inject_close(istream_t istream)
{
    struct istream_inject *inject = istream_to_inject(istream);

    assert(inject->input != NULL);

    istream_close_handler(inject->input);
    istream_deinit_abort(&inject->output);
}

static const struct istream istream_inject = {
    .available = istream_inject_available,
    .read = istream_inject_read,
    .close = istream_inject_close,
};


/*
 * constructor
 *
 */

istream_t
istream_inject_new(pool_t pool, istream_t input)
{
    assert(pool != NULL);
    assert(input != NULL);
    assert(!istream_has_handler(input));

    struct istream_inject *inject = istream_new_macro(pool, inject);
    istream_assign_handler(&inject->input, input,
                           &inject_input_handler, inject,
                           0);

    return istream_struct_cast(&inject->output);
}

void
istream_inject_fault(istream_t i_inject)
{
    struct istream_inject *inject = (struct istream_inject *)i_inject;

    if (inject->input != NULL)
        istream_close_handler(inject->input);

    istream_deinit_abort(&inject->output);
}
