/*
 * Fault injection istream filter.  This istream forwards data from
 * its input, but will never forward eof/abort.  The "abort" can be
 * injected at any time.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "istream-forward.h"

#include <assert.h>

struct istream_inject {
    struct istream output;
    struct istream *input;
};


/*
 * istream handler
 *
 */

static void
inject_input_eof(void *ctx)
{
    struct istream_inject *inject = ctx;

    assert(inject->input != NULL);

    inject->input = NULL;
}

static void
inject_input_abort(GError *error, void *ctx)
{
    struct istream_inject *inject = ctx;

    g_error_free(error);

    assert(inject->input != NULL);

    inject->input = NULL;
}

static const struct istream_handler inject_input_handler = {
    .data = istream_forward_data,
    .direct = istream_forward_direct,
    .eof = inject_input_eof,
    .abort = inject_input_abort,
};


/*
 * istream implementation
 *
 */

static inline struct istream_inject *
istream_to_inject(struct istream *istream)
{
    return (struct istream_inject *)(((char*)istream) - offsetof(struct istream_inject, output));
}

static off_t
istream_inject_available(struct istream *istream, bool partial)
{
    struct istream_inject *inject = istream_to_inject(istream);

    /* never return the total length, because the caller may then make
       assumptions on when this stream ends */

    return partial && inject->input != NULL
        ? istream_available(inject->input, partial)
        : -1;
}

static void
istream_inject_read(struct istream *istream)
{
    struct istream_inject *inject = istream_to_inject(istream);

    if (inject->input == NULL)
        return;

    istream_handler_set_direct(inject->input, inject->output.handler_direct);
    istream_read(inject->input);
}

static void
istream_inject_close(struct istream *istream)
{
    struct istream_inject *inject = istream_to_inject(istream);

    assert(inject->input != NULL);

    istream_close_handler(inject->input);
    istream_deinit(&inject->output);
}

static const struct istream_class istream_inject = {
    .available = istream_inject_available,
    .read = istream_inject_read,
    .close = istream_inject_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_inject_new(struct pool *pool, struct istream *input)
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
istream_inject_fault(struct istream *i_inject, GError *error)
{
    struct istream_inject *inject = (struct istream_inject *)i_inject;

    if (inject->input != NULL)
        istream_close_handler(inject->input);

    istream_deinit_abort(&inject->output, error);
}
