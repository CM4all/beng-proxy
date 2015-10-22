/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_memory.hxx"
#include "istream_oo.hxx"
#include "util/ConstBuffer.hxx"

#include <algorithm>

#include <stdint.h>

class MemoryIstream final : public Istream {
    ConstBuffer<uint8_t> data;

public:
    MemoryIstream(struct pool &p, const void *_data, size_t length)
        :Istream(p),
         data((const uint8_t *)_data, length) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) override {
        return data.size;
    }

    off_t _Skip(off_t length) override {
        size_t nbytes = std::min(off_t(data.size), length);
        data.skip_front(nbytes);
        return nbytes;
    }

    void _Read() override {
        if (!data.IsEmpty()) {
            auto nbytes = InvokeData(data.data, data.size);
            if (nbytes == 0)
                return;

            data.skip_front(nbytes);
        }

        if (data.IsEmpty())
            DestroyEof();
    }
};

struct istream *
istream_memory_new(struct pool *pool, const void *data, size_t length)
{
    return NewIstream<MemoryIstream>(*pool, data, length);
}
