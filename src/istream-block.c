/*
 * istream implementation which blocks indefinitely until closed.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"

struct istream_block {
    struct istream stream;
};

static inline struct istream_block *
istream_to_block(struct istream *istream)
{
    return (struct istream_block *)(((char*)istream) - offsetof(struct istream_block, stream));
}

static void
istream_block_read(struct istream *istream)
{
    struct istream_block *block = istream_to_block(istream);

    (void)block;
}

static void
istream_block_close(struct istream *istream)
{
    struct istream_block *block = istream_to_block(istream);

    istream_deinit(&block->stream);
}

static const struct istream_class istream_block = {
    .read = istream_block_read,
    .close = istream_block_close,
};

struct istream *
istream_block_new(struct pool *pool)
{
    struct istream_block *block = istream_new_macro(pool, block);
    return istream_struct_cast(&block->stream);
}
