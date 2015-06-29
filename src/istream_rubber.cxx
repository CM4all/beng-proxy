/*
 * istream implementation which reads from a rubber allocation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_rubber.hxx"
#include "istream/istream_oo.hxx"
#include "rubber.hxx"
#include "util/ConstBuffer.hxx"

#include <assert.h>
#include <stdint.h>

class RubberIstream final : public Istream {
    Rubber &rubber;
    const unsigned id;
    const bool auto_remove;

    size_t position;
    const size_t end;

public:
    RubberIstream(struct pool &p, Rubber &_rubber, unsigned _id,
                  size_t start, size_t _end,
                  bool _auto_remove)
        :Istream(p), rubber(_rubber), id(_id), auto_remove(_auto_remove),
         position(start), end(_end) {}

    /* virtual methods from class Istream */

    off_t GetAvailable(gcc_unused bool partial) override {
        return end - position;
    }

    off_t Skip(off_t nbytes) override {
        assert(position <= end);

        const size_t remaining = end - position;
        if (nbytes > off_t(remaining))
            nbytes = remaining;

        position += nbytes;
        return nbytes;
    }

    void Read() override {
        assert(position <= end);

        const uint8_t *data = (const uint8_t *)rubber_read(&rubber, id);
        const size_t remaining = end - position;

        if (remaining > 0) {
            size_t nbytes = InvokeData(data + position, remaining);
            if (nbytes == 0)
                return;

            position += nbytes;
        }

        if (position == end) {
            if (auto_remove)
                rubber_remove(&rubber, id);

            DestroyEof();
        }
    }

    void Close() override {
        if (auto_remove)
            rubber_remove(&rubber, id);

        Istream::Close();
    }
};

struct istream *
istream_rubber_new(struct pool *pool, Rubber *rubber,
                   unsigned id, size_t start, size_t end,
                   bool auto_remove)
{
    assert(rubber != nullptr);
    assert(id > 0);
    assert(start <= end);

    return NewIstream<RubberIstream>(*pool, *rubber, id,
                                     start, end, auto_remove);
}
