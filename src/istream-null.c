/*
 * istream implementation which reads nothing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

struct istream_null {
    struct istream stream;
};

static inline struct istream_null *
istream_to_null(istream_t istream)
{
    return (struct istream_null *)(((char*)istream) - offsetof(struct istream_null, stream));
}

static off_t
istream_null_available(istream_t istream __attr_unused,
                       bool partial __attr_unused)
{
    return 0;
}

static off_t
istream_null_skip(istream_t istream __attr_unused, off_t length __attr_unused)
{
    return 0;
}

static void
istream_null_read(istream_t istream)
{
    struct istream_null *null = istream_to_null(istream);

    istream_deinit_eof(&null->stream);
}

static void
istream_null_close(istream_t istream)
{
    struct istream_null *null = istream_to_null(istream);

    istream_deinit_abort(&null->stream);
}

static const struct istream istream_null = {
    .available = istream_null_available,
    .skip = istream_null_skip,
    .read = istream_null_read,
    .close = istream_null_close,
};

istream_t
istream_null_new(pool_t pool)
{
    struct istream_null *null = istream_new_macro(pool, null);
    return istream_struct_cast(&null->stream);
}
