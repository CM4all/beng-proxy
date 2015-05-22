/*
 * Fault injection istream filter.  This istream forwards data from
 * its input, but will never forward eof/abort.  The "abort" can be
 * injected at any time.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_inject.hxx"
#include "istream_internal.hxx"
#include "istream_forward.hxx"
#include "util/Cast.hxx"

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
    auto *inject = (struct istream_inject *)ctx;

    assert(inject->input != nullptr);

    inject->input = nullptr;
}

static void
inject_input_abort(GError *error, void *ctx)
{
    auto *inject = (struct istream_inject *)ctx;

    g_error_free(error);

    assert(inject->input != nullptr);

    inject->input = nullptr;
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
    return &ContainerCast2(*istream, &istream_inject::output);
}

static off_t
istream_inject_available(struct istream *istream, bool partial)
{
    struct istream_inject *inject = istream_to_inject(istream);

    /* never return the total length, because the caller may then make
       assumptions on when this stream ends */

    return partial && inject->input != nullptr
        ? istream_available(inject->input, partial)
        : -1;
}

static void
istream_inject_read(struct istream *istream)
{
    struct istream_inject *inject = istream_to_inject(istream);

    if (inject->input == nullptr)
        return;

    istream_handler_set_direct(inject->input, inject->output.handler_direct);
    istream_read(inject->input);
}

static void
istream_inject_close(struct istream *istream)
{
    struct istream_inject *inject = istream_to_inject(istream);

    assert(inject->input != nullptr);

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
    assert(pool != nullptr);
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    struct istream_inject *inject = istream_new_macro(pool, inject);
    istream_assign_handler(&inject->input, input,
                           &inject_input_handler, inject,
                           0);

    return &inject->output;
}

void
istream_inject_fault(struct istream *i_inject, GError *error)
{
    struct istream_inject *inject = (struct istream_inject *)i_inject;

    if (inject->input != nullptr)
        istream_close_handler(inject->input);

    istream_deinit_abort(&inject->output, error);
}
