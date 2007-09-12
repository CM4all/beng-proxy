/*
 * istream implementation which reads nothing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

static void
istream_null_read(istream_t istream)
{
    istream_invoke_eof(istream);
    istream_close(istream);
}

static void
istream_null_close(istream_t istream)
{
    istream_invoke_free(istream);
}

static const struct istream istream_null = {
    .read = istream_null_read,
    .close = istream_null_close,
};

istream_t
istream_null_new(pool_t pool)
{
    istream_t istream = p_malloc(pool, sizeof(*istream));

    *istream = istream_null;
    istream->pool = pool;

    return istream;
}
