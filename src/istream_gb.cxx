/*
 * A wrapper that turns a growing_buffer into an istream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_gb.hxx"
#include "istream-internal.h"
#include "growing_buffer.hxx"

#include <assert.h>
#include <string.h>

struct istream_gb {
    struct istream output;

    struct growing_buffer_reader reader;

    istream_gb(struct pool &pool, const struct growing_buffer &gb);
};

static off_t
istream_gb_available(struct istream *istream, bool partial gcc_unused)
{
    struct istream_gb *igb = (struct istream_gb *)istream;

    return igb->reader.Available();
}

static void
istream_gb_read(struct istream *istream)
{
    struct istream_gb *igb = (struct istream_gb *)istream;

    /* this loop is required to cross the buffer borders */
    while (1) {
        size_t length;
        const void *data = igb->reader.Read(&length);
        if (data == nullptr) {
            assert(igb->reader.IsEOF());
            istream_deinit_eof(&igb->output);
            return;
        }

        assert(!igb->reader.IsEOF());

        size_t nbytes = istream_invoke_data(&igb->output, data, length);
        if (nbytes == 0)
            /* growing_buffer has been closed */
            return;

        igb->reader.Consume(nbytes);
        if (nbytes < length)
            return;
    }
}

static void
istream_gb_close(struct istream *istream)
{
    struct istream_gb *igb = (struct istream_gb *)istream;

    istream_deinit(&igb->output);
}

static const struct istream_class istream_gb = {
    .available = istream_gb_available,
    .read = istream_gb_read,
    .close = istream_gb_close,
};

inline
istream_gb::istream_gb(struct pool &pool, const struct growing_buffer &gb)
    :output(::istream_gb, pool), reader(gb)
{
}

struct istream *
istream_gb_new(struct pool *pool, const struct growing_buffer *gb)
{
    assert(gb != nullptr);

    return &NewFromPool<struct istream_gb>(pool, *pool, *gb)->output;
}
