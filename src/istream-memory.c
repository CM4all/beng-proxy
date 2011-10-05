/*
 * istream implementation which reads from a fixed memory buffer.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "strref.h"

#include <assert.h>

struct istream_memory {
    struct istream stream;
    struct strref data;
};


static inline struct istream_memory *
istream_to_memory(istream_t istream)
{
    return (struct istream_memory *)(((char*)istream) - offsetof(struct istream_memory, stream));
}

static off_t
istream_memory_available(istream_t istream, bool partial gcc_unused)
{
    struct istream_memory *memory = istream_to_memory(istream);

    return memory->data.length;
}

static void
istream_memory_read(istream_t istream)
{
    struct istream_memory *memory = istream_to_memory(istream);
    size_t nbytes;

    if (!strref_is_empty(&memory->data)) {
        nbytes = istream_invoke_data(&memory->stream,
                                     memory->data.data, memory->data.length);
        if (nbytes == 0)
            return;

        strref_skip(&memory->data, nbytes);
    }

    if (strref_is_empty(&memory->data))
        istream_deinit_eof(&memory->stream);
}

static void
istream_memory_close(istream_t istream)
{
    struct istream_memory *memory = istream_to_memory(istream);

    istream_deinit(&memory->stream);
}

static const struct istream istream_memory = {
    .available = istream_memory_available,
    .read = istream_memory_read,
    .close = istream_memory_close,
};

istream_t
istream_memory_new(struct pool *pool, const void *data, size_t length)
{
    struct istream_memory *memory = istream_new_macro(pool, memory);

    assert(data != NULL);

    strref_set(&memory->data, data, length);

    return istream_struct_cast(&memory->stream);
}
