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
istream_to_block(istream_t istream)
{
    return (struct istream_block *)(((char*)istream) - offsetof(struct istream_block, stream));
}

static void
istream_block_read(istream_t istream)
{
    struct istream_block *block = istream_to_block(istream);

    (void)block;
}

static void
istream_block_close(istream_t istream)
{
    struct istream_block *block = istream_to_block(istream);

    istream_deinit_abort(&block->stream);
}

static const struct istream istream_block = {
    .read = istream_block_read,
    .close = istream_block_close,
};

istream_t
istream_block_new(pool_t pool)
{
    struct istream_block *block = istream_new_macro(pool, block);
    return istream_struct_cast(&block->stream);
}
