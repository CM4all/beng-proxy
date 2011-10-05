/*
 * A wrapper that turns a growing_buffer into an istream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-gb.h"
#include "istream-internal.h"
#include "growing-buffer.h"

#include <assert.h>
#include <string.h>

struct istream_gb {
    struct istream output;

    struct growing_buffer_reader reader;
};

static off_t
istream_gb_available(istream_t istream, bool partial gcc_unused)
{
    struct istream_gb *igb = (struct istream_gb *)istream;

    return growing_buffer_reader_available(&igb->reader);
}

static void
istream_gb_read(istream_t istream)
{
    struct istream_gb *igb = (struct istream_gb *)istream;

    /* this loop is required to cross the buffer borders */
    while (1) {
        size_t length;
        const void *data = growing_buffer_reader_read(&igb->reader, &length);
        if (data == NULL) {
            assert(growing_buffer_reader_eof(&igb->reader));
            istream_deinit_eof(&igb->output);
            return;
        }

        assert(!growing_buffer_reader_eof(&igb->reader));

        size_t nbytes = istream_invoke_data(&igb->output, data, length);
        if (nbytes == 0)
            /* growing_buffer has been closed */
            return;

        growing_buffer_reader_consume(&igb->reader, nbytes);
        if (nbytes < length)
            return;
    }
}

static void
istream_gb_close(istream_t istream)
{
    struct istream_gb *igb = (struct istream_gb *)istream;

    istream_deinit(&igb->output);
}

static const struct istream istream_gb = {
    .available = istream_gb_available,
    .read = istream_gb_read,
    .close = istream_gb_close,
};

istream_t
istream_gb_new(struct pool *pool, const struct growing_buffer *gb)
{
    assert(gb != NULL);

    struct istream_gb *igb = istream_new_macro(pool, gb);
    growing_buffer_reader_init(&igb->reader, gb);

    return istream_struct_cast(&igb->output);
}
