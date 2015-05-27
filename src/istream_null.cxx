/*
 * istream implementation which reads nothing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_null.hxx"
#include "istream_oo.hxx"

#include <unistd.h>

class NullIstream : public Istream {
public:
    NullIstream(struct pool &p)
        :Istream(p, MakeIstreamClass<NullIstream>::cls) {}

    off_t GetAvailable(gcc_unused bool partial) {
        return 0;
    }

    void Read() {
        DestroyEof();
    }

    int AsFd() {
        /* fd0 is always linked with /dev/null */
        int fd = dup(0);
        if (fd < 0)
            return -1;

        Destroy();
        return fd;
    }

    void Close() {
        Destroy();
    }
};

struct istream *
istream_null_new(struct pool *pool)
{
    return NewIstream<NullIstream>(*pool);
}
