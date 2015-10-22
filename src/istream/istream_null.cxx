/*
 * istream implementation which reads nothing.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_null.hxx"
#include "istream_oo.hxx"

#include <unistd.h>

class NullIstream final : public Istream {
public:
    NullIstream(struct pool &p)
        :Istream(p) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) override {
        return 0;
    }

    void _Read() override {
        DestroyEof();
    }

    int _AsFd() override {
        /* fd0 is always linked with /dev/null */
        int fd = dup(0);
        if (fd < 0)
            return -1;

        Destroy();
        return fd;
    }

    void _Close() override {
        Destroy();
    }
};

struct istream *
istream_null_new(struct pool *pool)
{
    return NewIstream<NullIstream>(*pool);
}
