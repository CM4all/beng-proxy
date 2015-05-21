/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_memory.hxx"
#include "istream-internal.h"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"

#include <assert.h>
#include <stdint.h>

struct istream_memory {
    struct istream stream;

    ConstBuffer<uint8_t> data;
};

static inline struct istream_memory *
istream_to_memory(struct istream *istream)
{
    return &ContainerCast2(*istream, &istream_memory::stream);
}

static off_t
istream_memory_available(struct istream *istream, bool partial gcc_unused)
{
    struct istream_memory *memory = istream_to_memory(istream);

    return memory->data.size;
}

static void
istream_memory_read(struct istream *istream)
{
    struct istream_memory *memory = istream_to_memory(istream);
    size_t nbytes;

    if (!memory->data.IsEmpty()) {
        nbytes = istream_invoke_data(&memory->stream,
                                     memory->data.data, memory->data.size);
        if (nbytes == 0)
            return;

        memory->data.skip_front(nbytes);
    }

    if (memory->data.IsEmpty())
        istream_deinit_eof(&memory->stream);
}

static void
istream_memory_close(struct istream *istream)
{
    struct istream_memory *memory = istream_to_memory(istream);

    istream_deinit(&memory->stream);
}

static const struct istream_class istream_memory = {
    .available = istream_memory_available,
    .read = istream_memory_read,
    .close = istream_memory_close,
};

struct istream *
istream_memory_new(struct pool *pool, const void *data, size_t length)
{
    struct istream_memory *memory = istream_new_macro(pool, memory);

    assert(data != nullptr);

    memory->data = { (const uint8_t *)data, length };

    return &memory->stream;
}
