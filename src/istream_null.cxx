/*
 * istream implementation which reads nothing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_null.hxx"
#include "istream_internal.hxx"
#include "util/Cast.hxx"
#include "pool.hxx"

#include <unistd.h>

struct NullIstream {
    struct istream stream;

    NullIstream(struct pool &p);
};

static inline NullIstream &
istream_to_null(struct istream *istream)
{
    return ContainerCast2(*istream, &NullIstream::stream);
}

static off_t
istream_null_available(struct istream *istream gcc_unused,
                       bool partial gcc_unused)
{
    return 0;
}

static off_t
istream_null_skip(struct istream *istream gcc_unused, off_t length gcc_unused)
{
    return 0;
}

static void
istream_null_read(struct istream *istream)
{
    NullIstream &null = istream_to_null(istream);

    istream_deinit_eof(&null.stream);
}

static int
istream_null_as_fd(struct istream *istream)
{
    NullIstream &null = istream_to_null(istream);

    /* fd0 is always linked with /dev/null */
    int fd = dup(0);
    if (fd < 0)
        return -1;

    istream_deinit(&null.stream);
    return fd;
}

static void
istream_null_close(struct istream *istream)
{
    NullIstream &null = istream_to_null(istream);

    istream_deinit(&null.stream);
}

static constexpr struct istream_class istream_null = {
    .available = istream_null_available,
    .skip = istream_null_skip,
    .read = istream_null_read,
    .as_fd = istream_null_as_fd,
    .close = istream_null_close,
};

inline NullIstream::NullIstream(struct pool &p)
{
    istream_init(&stream, &istream_null, &p);
}

struct istream *
istream_null_new(struct pool *pool)
{
    auto *n = NewFromPool<NullIstream>(*pool, *pool);
    return &n->stream;
}
