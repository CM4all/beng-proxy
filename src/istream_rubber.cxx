/*
 * istream implementation which reads from a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_rubber.hxx"
#include "istream-internal.h"
#include "rubber.hxx"

#include <assert.h>

struct istream_rubber {
    struct istream base;

    Rubber *rubber;
    unsigned id;

    size_t position, end;

    bool auto_remove;
};

static inline struct istream_rubber *
istream_to_rubber(struct istream *istream)
{
    void *p = ((char *)istream) - offsetof(struct istream_rubber, base);
    return (struct istream_rubber *)p;
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

    const uint8_t *data = (const uint8_t *)rubber_read(r->rubber, r->id);
    const size_t remaining = r->end - r->position;

    if (remaining > 0) {
        size_t nbytes = istream_invoke_data(&r->base, data + r->position,
                                            remaining);
        if (nbytes == 0)
            return;

        r->position += nbytes;
    }

    if (r->position == r->end) {
        if (r->auto_remove)
            rubber_remove(r->rubber, r->id);

        istream_deinit_eof(&r->base);
    }
}

static void
istream_rubber_close(struct istream *istream)
{
    struct istream_rubber *r = istream_to_rubber(istream);

    if (r->auto_remove)
        rubber_remove(r->rubber, r->id);

    istream_deinit(&r->base);
}

static const struct istream_class istream_rubber = {
    .available = istream_rubber_available,
    .skip = istream_rubber_skip,
    .read = istream_rubber_read,
    .close = istream_rubber_close,
};

struct istream *
istream_rubber_new(struct pool *pool, Rubber *rubber,
                   unsigned id, size_t start, size_t end,
                   bool auto_remove)
{
    assert(rubber != nullptr);
    assert(id > 0);
    assert(start <= end);

    struct istream_rubber *r = istream_new_macro(pool, rubber);
    r->rubber = rubber;
    r->id = id;
    r->position = start;
    r->end = end;
    r->auto_remove = auto_remove;

    return istream_struct_cast(&r->base);
}
