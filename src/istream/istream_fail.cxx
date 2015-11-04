/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_fail.hxx"
#include "istream_oo.hxx"

#include <glib.h>

#include <unistd.h>

class FailIstream final : public Istream {
    GError *const error;

public:
    FailIstream(struct pool &p, GError *_error)
        :Istream(p), error(_error) {}

    /* virtual methods from class Istream */

    void _Read() override {
        DestroyError(error);
    }

    void _Close() override {
        g_error_free(error);
        Istream::_Close();
    }
};

Istream *
istream_fail_new(struct pool *pool, GError *error)
{
    assert(pool != nullptr);
    assert(error != nullptr);

    return NewIstream<FailIstream>(*pool, error);
}
