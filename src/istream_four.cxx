/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_four.hxx"
#include "istream_forward.hxx"

#include <algorithm>

class FourIstream final : public ForwardIstream {
public:
    FourIstream(struct pool &p, struct istream &_input)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<FourIstream>::handler, this) {}

    /* virtual methods from class Istream */

    off_t GetAvailable(gcc_unused bool partial) override {
        return -1;
    }

    off_t Skip(gcc_unused off_t length) override {
        return -1;
    }

    int AsFd() override {
        return -1;
    }

    /* handler */

    size_t OnData(const void *data, size_t length) {
        return ForwardIstream::OnData(data,
                                      std::min(length, size_t(4)));
    }

    ssize_t OnDirect(enum istream_direct type, int fd, size_t max_length) {
        return ForwardIstream::OnDirect(type, fd,
                                        std::min(max_length, size_t(4)));
    }
};

struct istream *
istream_four_new(struct pool *pool, struct istream *input)
{
    return NewIstream<FourIstream>(*pool, *input);
}
