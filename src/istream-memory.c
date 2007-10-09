/*
 * istream implementation which reads from a fixed memory buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"

#include <assert.h>

struct istream_memory {
    struct istream stream;
    const char *data;
    size_t length;
};


static void
memory_close(struct istream_memory *memory)
{
    memory->data = NULL;

    istream_invoke_free(&memory->stream);
}


static inline struct istream_memory *
istream_to_memory(istream_t istream)
{
    return (struct istream_memory *)(((char*)istream) - offsetof(struct istream_memory, stream));
}

static void
istream_memory_read(istream_t istream)
{
    struct istream_memory *memory = istream_to_memory(istream);
    size_t nbytes;

    assert(memory->data != NULL);

    if (memory->length > 0) {
        nbytes = istream_invoke_data(&memory->stream, memory->data, memory->length);
        assert(nbytes <= memory->length);

        if (memory->data == NULL)
            return;

        memory->data += nbytes;
        memory->length -= nbytes;
    }

    if (memory->length == 0) {
        istream_invoke_eof(&memory->stream);
        memory_close(memory);
    }
}

static void
istream_memory_close(istream_t istream)
{
    struct istream_memory *memory = istream_to_memory(istream);

    memory_close(memory);
}

static const struct istream istream_memory = {
    .read = istream_memory_read,
    .close = istream_memory_close,
};

istream_t
istream_memory_new(pool_t pool, const void *data, size_t length)
{
    struct istream_memory *memory = p_malloc(pool, sizeof(*memory));

    assert(data != NULL);

    memory->stream = istream_memory;
    memory->stream.pool = pool;
    memory->data = data;
    memory->length = length;

    return istream_struct_cast(&memory->stream);
}
