/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_zero.hxx"
#include "istream_oo.hxx"

#include <limits.h>

class ZeroIstream final : public Istream {
public:
    explicit ZeroIstream(struct pool &_pool):Istream(_pool) {}

    /* virtual methods from class Istream */

    off_t GetAvailable(bool partial) override {
        return partial
            ? INT_MAX
            : -1;
    }

    off_t Skip(off_t length) override {
        return length;
    }

    void Read() override {
        static char buffer[1024];

        InvokeData(buffer, sizeof(buffer));
    }
};

struct istream *
istream_zero_new(struct pool *pool)
{
    return NewIstream(*pool);
}
