/*
 * istream implementation which reads nothing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

struct istream_null {
    struct istream stream;
};

static inline struct istream_null *
istream_to_null(istream_t istream)
{
    return (struct istream_null *)(((char*)istream) - offsetof(struct istream_null, stream));
}

static void
istream_null_read(istream_t istream)
{
    struct istream_null *null = istream_to_null(istream);

    istream_invoke_eof(&null->stream);
    istream_invoke_free(&null->stream);
}

static void
istream_null_close(istream_t istream)
{
    struct istream_null *null = istream_to_null(istream);

    istream_invoke_free(&null->stream);
}

static const struct istream istream_null = {
    .read = istream_null_read,
    .close = istream_null_close,
};

istream_t
istream_null_new(pool_t pool)
{
    struct istream_null *null = p_malloc(pool, sizeof(*null));

    null->stream = istream_null;
    null->stream.pool = pool;

    return istream_struct_cast(&null->stream);
}
