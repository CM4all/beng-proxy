/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_fail.hxx"
#include "istream_oo.hxx"

#include <unistd.h>

class FailIstream : public Istream {
    GError *const error;

public:
    FailIstream(struct pool &p, GError *_error)
        :Istream(p, MakeIstreamClass<FailIstream>::cls),
         error(_error) {}

    void Read() {
        DestroyError(error);
    }

    void Close() {
        g_error_free(error);
        Istream::Close();
    }
};

struct istream *
istream_fail_new(struct pool *pool, GError *error)
{
    assert(pool != nullptr);
    assert(error != nullptr);

    return NewIstream<FailIstream>(*pool, error);
}
