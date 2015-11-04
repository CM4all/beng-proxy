/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_four.hxx"
#include "ForwardIstream.hxx"

#include <algorithm>

class FourIstream final : public ForwardIstream {
public:
    FourIstream(struct pool &p, Istream &_input)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<FourIstream>::handler, this) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(gcc_unused bool partial) override {
        return -1;
    }

    off_t _Skip(gcc_unused off_t length) override {
        return -1;
    }

    int _AsFd() override {
        return -1;
    }

    /* handler */

    size_t OnData(const void *data, size_t length) {
        return ForwardIstream::OnData(data,
                                      std::min(length, size_t(4)));
    }

    ssize_t OnDirect(FdType type, int fd, size_t max_length) {
        return ForwardIstream::OnDirect(type, fd,
                                        std::min(max_length, size_t(4)));
    }
};

Istream *
istream_four_new(struct pool *pool, Istream &input)
{
    return NewIstream<FourIstream>(*pool, input);
}
