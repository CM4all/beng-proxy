/*
 * This istream filter passes one byte at a time.  This is useful for
 * testing and debugging istream handler implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_byte.hxx"
#include "ForwardIstream.hxx"

class ByteIstream final : public ForwardIstream {
public:
    ByteIstream(struct pool &p, Istream &_input)
        :ForwardIstream(p, _input,
                        MakeIstreamHandler<ByteIstream>::handler, this) {}

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

    size_t OnData(const void *data, gcc_unused size_t length) {
        return ForwardIstream::OnData(data, 1);
    }

    ssize_t OnDirect(FdType type, int fd,
                     gcc_unused size_t max_length) {
        return ForwardIstream::OnDirect(type, fd, 1);
    }
};

Istream *
istream_byte_new(struct pool &pool, Istream &input)
{
    return NewIstream<ByteIstream>(pool, input);
}
