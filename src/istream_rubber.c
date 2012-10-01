/*
 * istream implementation which reads from a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_rubber.h"
#include "istream-internal.h"
#include "rubber.h"

#include <assert.h>

struct istream_rubber {
    struct istream base;

    const struct rubber *rubber;
    unsigned id;

    size_t position, end;
};

static inline struct istream_rubber *
istream_to_rubber(struct istream *istream)
{
    return (struct istream_rubber *)(((char*)istream) - offsetof(struct istream_rubber, base));
}

static off_t
istream_rubber_available(struct istream *istream, bool partial gcc_unused)
{
    struct istream_rubber *r = istream_to_rubber(istream);
    assert(r->position <= r->end);

    return r->end - r->position;
}

static off_t
istream_rubber_skip(struct istream *istream, off_t _nbytes)
{
    assert(_nbytes >= 0);

    struct istream_rubber *r = istream_to_rubber(istream);
    assert(r->position <= r->end);

    const size_t remaining = r->end - r->position;
    size_t nbytes = (size_t)_nbytes;
    if (nbytes > remaining)
        nbytes = remaining;

    r->position += nbytes;
    return nbytes;
}

static void
istream_rubber_read(struct istream *istream)
{
    struct istream_rubber *r = istream_to_rubber(istream);
    assert(r->position <= r->end);

    const uint8_t *data = rubber_read(r->rubber, r->id);
    const size_t remaining = r->end - r->position;

    if (remaining > 0) {
        size_t nbytes = istream_invoke_data(&r->base, data, remaining);
        if (nbytes == 0)
            return;

        r->position += nbytes;
    }

    if (r->position == r->end)
        istream_deinit_eof(&r->base);
}

static void
istream_rubber_close(struct istream *istream)
{
    struct istream_rubber *r = istream_to_rubber(istream);

    istream_deinit(&r->base);
}

static const struct istream_class istream_rubber = {
    .available = istream_rubber_available,
    .skip = istream_rubber_skip,
    .read = istream_rubber_read,
    .close = istream_rubber_close,
};

struct istream *
istream_rubber_new(struct pool *pool, const struct rubber *rubber,
                   unsigned id, size_t start, size_t end)
{
    assert(rubber != NULL);
    assert(id > 0);
    assert(start <= end);

    struct istream_rubber *r = istream_new_macro(pool, rubber);
    r->rubber = rubber;
    r->id = id;
    r->position = start;
    r->end = end;

    return istream_struct_cast(&r->base);
}
