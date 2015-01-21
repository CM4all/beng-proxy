/*
 * istream implementation which blocks indefinitely until closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_block.hxx"
#include "istream-internal.h"
#include "pool.hxx"
#include "util/Cast.hxx"

struct BlockIstream {
    struct istream stream;

    BlockIstream(struct pool &p);
};

static inline BlockIstream &
istream_to_block(struct istream *istream)
{
    return ContainerCast2(*istream, &BlockIstream::stream);
}

static void
istream_block_read(struct istream *istream)
{
    BlockIstream &block = istream_to_block(istream);

    (void)block;
}

static void
istream_block_close(struct istream *istream)
{
    BlockIstream &block = istream_to_block(istream);

    istream_deinit(&block.stream);
}

static constexpr struct istream_class istream_block = {
    .read = istream_block_read,
    .close = istream_block_close,
};

inline BlockIstream::BlockIstream(struct pool &p)
{
    istream_init(&stream, &istream_block, &p);
}

struct istream *
istream_block_new(struct pool &pool)
{
    auto *block = NewFromPool<BlockIstream>(pool, pool);
    return &block->stream;
}
