/*
 * istream implementation which reads nothing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

#include <limits.h>

struct istream_zero {
    struct istream stream;
};

static inline struct istream_zero *
istream_to_zero(istream_t istream)
{
    return (struct istream_zero *)(((char*)istream) - offsetof(struct istream_zero, stream));
}

static off_t
istream_zero_available(__attr_unused istream_t istream, bool partial)
{
    return partial
        ? INT_MAX
        : -1;
}

static off_t
istream_zero_skip(istream_t istream __attr_unused, off_t length)
{
    return length;
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

    istream_deinit(&zero->stream);
}

static const struct istream istream_zero = {
    .available = istream_zero_available,
    .skip = istream_zero_skip,
    .read = istream_zero_read,
    .close = istream_zero_close,
};

istream_t
istream_zero_new(pool_t pool)
{
    struct istream_zero *zero = istream_new_macro(pool, zero);
    return istream_struct_cast(&zero->stream);
}
