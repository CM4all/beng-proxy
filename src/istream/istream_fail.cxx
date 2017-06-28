/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_fail.hxx"
#include "istream.hxx"

class FailIstream final : public Istream {
    const std::exception_ptr error;

public:
    FailIstream(struct pool &p, std::exception_ptr _error)
        :Istream(p), error(_error) {}

    /* virtual methods from class Istream */

    void _Read() override {
        assert(error);
        DestroyError(error);
    }

    void _FillBucketList(gcc_unused IstreamBucketList &list) override {
        auto copy = error;
        Destroy();
        std::rethrow_exception(copy);
    }
};

Istream *
istream_fail_new(struct pool *pool, std::exception_ptr ep)
{
    assert(pool != nullptr);
    assert(ep);

    return NewIstream<FailIstream>(*pool, ep);
}
