/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_memory.hxx"
#include "istream_oo.hxx"
#include "util/ConstBuffer.hxx"

#include <algorithm>

#include <stdint.h>

class MemoryIstream : public Istream {
    ConstBuffer<uint8_t> data;

public:
    MemoryIstream(struct pool &p, const void *_data, size_t length)
        :Istream(p, MakeIstreamClass<MemoryIstream>::cls),
         data((const uint8_t *)_data, length) {}

    off_t Available(gcc_unused bool partial) {
        return data.size;
    }

    off_t Skip(off_t length) {
        size_t nbytes = std::min(off_t(data.size), length);
        data.skip_front(nbytes);
        return nbytes;
    }

    void Read() {
        if (!data.IsEmpty()) {
            auto nbytes = InvokeData(data.data, data.size);
            if (nbytes == 0)
                return;

            data.skip_front(nbytes);
        }

        if (data.IsEmpty())
            DeinitEof();
    }

    int AsFd() {
        return -1;
    }

    void Close() {
        Deinit();
    }
};

struct istream *
istream_memory_new(struct pool *pool, const void *data, size_t length)
{
    return NewIstream<MemoryIstream>(*pool, data, length);
}
