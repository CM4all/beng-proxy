/*
 * This istream filter passes one byte at a time.  This is useful for
 * testing and debugging istream handler implementations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_byte.hxx"
#include "istream_forward.hxx"

class ByteIstream : public ForwardIstream {
public:
    ByteIstream(struct pool &p, struct istream &_input)
        :ForwardIstream(p, MakeIstreamClass<ByteIstream>::cls,
                        _input,
                        MakeIstreamHandler<ByteIstream>::handler, this) {}

    /* istream */

    off_t Available(gcc_unused bool partial) {
        return -1;
    }

    off_t Skip(gcc_unused off_t length) {
        return -1;
    }

    int AsFd() {
        return -1;
    }

    /* handler */

    size_t OnData(const void *data, gcc_unused size_t length) {
        return ForwardIstream::OnData(data, 1);
    }

    ssize_t OnDirect(enum istream_direct type, int fd,
                     gcc_unused size_t max_length) {
        return ForwardIstream::OnDirect(type, fd, 1);
    }
};

struct istream *
istream_byte_new(struct pool &pool, struct istream &input)
{
    return NewIstream<ByteIstream>(pool, input);
}
