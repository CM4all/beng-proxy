/*
 * istream implementation which produces a failure.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

struct istream_fail {
    struct istream stream;

    GError *error;
};

static inline struct istream_fail *
istream_to_fail(istream_t istream)
{
    return (struct istream_fail *)(((char*)istream) - offsetof(struct istream_fail, stream));
}

static void
istream_fail_read(istream_t istream)
{
    struct istream_fail *fail = istream_to_fail(istream);

    istream_deinit_abort(&fail->stream, fail->error);
}

static void
istream_fail_close(istream_t istream)
{
    struct istream_fail *fail = istream_to_fail(istream);

    g_error_free(fail->error);
    istream_deinit(&fail->stream);
}

static const struct istream istream_fail = {
    .read = istream_fail_read,
    .close = istream_fail_close,
};

istream_t
istream_fail_new(pool_t pool, GError *error)
{
    assert(pool != NULL);
    assert(error != NULL);

    struct istream_fail *fail = istream_new_macro(pool, fail);
    fail->error = error;
    return istream_struct_cast(&fail->stream);
}
