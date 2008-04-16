/*
 * istream implementation which reads nothing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

struct istream_zero {
    struct istream stream;
};

static inline struct istream_zero *
istream_to_zero(istream_t istream)
{
    return (struct istream_zero *)(((char*)istream) - offsetof(struct istream_zero, stream));
}

static void
istream_zero_read(istream_t istream)
{
    struct istream_zero *zero = istream_to_zero(istream);
    static char buffer[1024];

    istream_invoke_data(&zero->stream, buffer, sizeof(buffer));
}

static void
istream_zero_close(istream_t istream)
{
    struct istream_zero *zero = istream_to_zero(istream);

    istream_deinit_abort(&zero->stream);
}

static const struct istream istream_zero = {
    .read = istream_zero_read,
    .close = istream_zero_close,
};

istream_t
istream_zero_new(pool_t pool)
{
    struct istream_zero *zero = istream_new_macro(pool, zero);
    return istream_struct_cast(&zero->stream);
}
