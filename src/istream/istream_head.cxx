/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_head.hxx"
#include "ForwardIstream.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

class HeadIstream final : public ForwardIstream {
    off_t rest;
    const bool authoritative;

public:
    HeadIstream(struct pool &p, Istream &_input,
                size_t size, bool _authoritative)
        :ForwardIstream(p, _input),
         rest(size), authoritative(_authoritative) {}

    /* virtual methods from class Istream */

    off_t _GetAvailable(bool partial) override;
    off_t _Skip(off_t length) override;
    void _Read() override;

    int _AsFd() override {
        return -1;
    }

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    ssize_t OnDirect(FdType type, int fd, size_t max_length) override;
};

/*
 * istream handler
 *
 */

size_t
HeadIstream::OnData(const void *data, size_t length)
{
    if (rest == 0) {
        input.Close();
        DestroyEof();
        return 0;
    }

    if ((off_t)length > rest)
        length = rest;

    size_t nbytes = InvokeData(data, length);
    assert((off_t)nbytes <= rest);

    if (nbytes > 0) {
        rest -= nbytes;
        if (rest == 0) {
            input.Close();
            DestroyEof();
            return 0;
        }
    }

    return nbytes;
}

ssize_t
HeadIstream::OnDirect(FdType type, int fd, size_t max_length)
{
    if (rest == 0) {
        input.Close();
        DestroyEof();
        return ISTREAM_RESULT_CLOSED;
    }

    if ((off_t)max_length > rest)
        max_length = rest;

    ssize_t nbytes = InvokeDirect(type, fd, max_length);
    assert(nbytes < 0 || (off_t)nbytes <= rest);

    if (nbytes > 0) {
        rest -= (size_t)nbytes;
        if (rest == 0) {
            input.Close();
            DestroyEof();
            return ISTREAM_RESULT_CLOSED;
        }
    }

    return nbytes;
}

/*
 * istream implementation
 *
 */

off_t
HeadIstream::_GetAvailable(bool partial)
{
    if (authoritative) {
        assert(partial ||
               input.GetAvailable(partial) < 0 ||
               input.GetAvailable(partial) >= (off_t)rest);
        return rest;
    }

    off_t available = input.GetAvailable(partial);
    return std::min(available, rest);
}

off_t
HeadIstream::_Skip(off_t length)
{
    if (length >= rest)
        length = rest;

    off_t nbytes = ForwardIstream::Skip(length);
    assert(nbytes <= length);

    if (nbytes > 0)
        rest -= nbytes;

    return nbytes;
}

void
HeadIstream::_Read()
{
    if (rest == 0) {
        input.Close();
        DestroyEof();
    } else {
        ForwardIstream::_Read();
    }
}

/*
 * constructor
 *
 */

Istream *
istream_head_new(struct pool *pool, Istream &input,
                 size_t size, bool authoritative)
{
    return NewIstream<HeadIstream>(*pool, input, size, authoritative);
}
