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
        nbytes = istream_invoke_data(istream, memory->data, memory->length);
        assert(nbytes <= memory->length);

        memory->data += nbytes;
        memory->length -= nbytes;
    }

    if (memory->length == 0) {
        istream_invoke_eof(istream);
        istream_close(istream);
    }
}

static void
istream_memory_close(istream_t istream)
{
    struct istream_memory *memory = istream_to_memory(istream);

    memory->data = NULL;

    istream_invoke_free(istream);
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
    memory->data = data;
    memory->length = length;

    return &memory->stream;
}
