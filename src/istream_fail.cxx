/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_fail.hxx"
#include "istream-internal.h"
#include "util/Cast.hxx"

#include <glib.h>

struct istream_fail {
    struct istream stream;

    GError *error;
};

static inline struct istream_fail *
istream_to_fail(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_fail::stream);
}

static void
istream_fail_read(struct istream *istream)
{
    struct istream_fail *fail = istream_to_fail(istream);

    istream_deinit_abort(&fail->stream, fail->error);
}

static void
istream_fail_close(struct istream *istream)
{
    struct istream_fail *fail = istream_to_fail(istream);

    g_error_free(fail->error);
    istream_deinit(&fail->stream);
}

static const struct istream_class istream_fail = {
    .read = istream_fail_read,
    .close = istream_fail_close,
};

struct istream *
istream_fail_new(struct pool *pool, GError *error)
{
    assert(pool != nullptr);
    assert(error != nullptr);

    struct istream_fail *fail = istream_new_macro(pool, fail);
    fail->error = error;
    return &fail->stream;
}
