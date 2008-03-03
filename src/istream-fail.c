/*
 * istream implementation which produces a failure.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

struct istream_fail {
    struct istream stream;
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

    istream_invoke_abort(&fail->stream);
}

static void
istream_fail_close(istream_t istream)
{
    struct istream_fail *fail = istream_to_fail(istream);

    istream_invoke_abort(&fail->stream);
}

static const struct istream istream_fail = {
    .read = istream_fail_read,
    .close = istream_fail_close,
};

istream_t
istream_fail_new(pool_t pool)
{
    struct istream_fail *fail = p_malloc(pool, sizeof(*fail));

    fail->stream = istream_fail;
    fail->stream.pool = pool;

    return istream_struct_cast(&fail->stream);
}
