/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_body.hxx"
#include "istream/istream_dechunk.hxx"
#include "istream/Bucket.hxx"
#include "filtered_socket.hxx"

#include <assert.h>
#include <limits.h>

gcc_pure
off_t
HttpBodyReader::GetAvailable(const FilteredSocket &s, bool partial) const
{
    assert(rest != REST_EOF_CHUNK);

    if (KnownLength())
        return rest;

    return partial
        ? (off_t)s.GetAvailable()
        : -1;
}

void
HttpBodyReader::FillBucketList(const FilteredSocket &s,
                               IstreamBucketList &list)
{
    auto b = s.ReadBuffer();
    if (b.IsEmpty()) {
        if (!IsEOF())
            list.SetMore();
        return;
    }

    size_t max = GetMaxRead(b.size);
    if (b.size > max)
        b.size = max;

    list.Push(ConstBuffer<void>(b.data, b.size));
    if ((off_t)b.size != rest)
        list.SetMore();
}

size_t
HttpBodyReader::ConsumeBucketList(FilteredSocket &s, size_t nbytes)
{
    auto b = s.ReadBuffer();
    if (b.IsEmpty())
        return 0;

    size_t max = GetMaxRead(b.size);
    if (nbytes > max)
        nbytes = max;
    if (nbytes == 0)
        return 0;

    s.Consumed(nbytes);
    Consumed(nbytes);
    Istream::Consumed(nbytes);
    return nbytes;
}

/** determine how much can be read from the body */
size_t
HttpBodyReader::GetMaxRead(size_t length) const
{
    assert(rest != REST_EOF_CHUNK);

    if (KnownLength() && rest < (off_t)length)
        /* content-length header was provided, return this value */
        return (size_t)rest;
    else
        /* read as much as possible, the dechunker will do the rest */
        return length;
}

void
HttpBodyReader::Consumed(size_t nbytes)
{
    if (!KnownLength())
        return;

    assert((off_t)nbytes <= rest);

    rest -= (off_t)nbytes;
}

size_t
HttpBodyReader::FeedBody(const void *data, size_t length)
{
    assert(length > 0);

    length = GetMaxRead(length);
    size_t consumed = InvokeData(data, length);
    if (consumed > 0)
        Consumed(consumed);

    return consumed;
}

ssize_t
HttpBodyReader::TryDirect(int fd, FdType fd_type)
{
    assert(fd >= 0);
    assert(CheckDirect(fd_type));

    ssize_t nbytes = InvokeDirect(fd_type, fd, GetMaxRead(INT_MAX));
    if (nbytes > 0)
        Consumed((size_t)nbytes);

    return nbytes;
}

bool
HttpBodyReader::IsSocketDone(const FilteredSocket &s) const
{
    return KnownLength() &&
        (IsEOF() || (off_t)s.GetAvailable() >= rest);
}

bool
HttpBodyReader::SocketEOF(size_t remaining)
{
    if (rest == REST_UNKNOWN) {
        if (remaining > 0) {
            /* serve the rest of the buffer, then end the body
               stream */
            rest = remaining;
            return true;
        }

        /* the socket is closed, which ends the body */
        InvokeEof();
        return false;
    } else if (rest == (off_t)remaining ||
               rest == REST_CHUNKED ||
               rest == REST_EOF_CHUNK) {
        if (remaining > 0)
            /* serve the rest of the buffer, then end the body
               stream */
            return true;

        InvokeEof();
        return false;
    } else {
        /* something has gone wrong: either not enough or too much
           data left in the buffer */
        GError *error = g_error_new_literal(buffered_socket_quark(), 0,
                                            "premature end of socket");
        InvokeError(error);
        return false;
    }
}

void
HttpBodyReader::OnDechunkEndSeen()
{
    assert(rest == REST_CHUNKED);

    end_seen = true;
}

void
HttpBodyReader::OnDechunkEnd(gcc_unused Istream *input)

{
    assert(rest == REST_CHUNKED);
    assert(input == nullptr || input == this);

    rest = REST_EOF_CHUNK;
}

Istream &
HttpBodyReader::Init(off_t content_length, bool _chunked)
{
    assert(content_length >= -1);

    rest = content_length;

    Istream *s = this;
    if (_chunked) {
        assert(rest == (off_t)REST_UNKNOWN);

        rest = REST_CHUNKED;
        end_seen = false;

        s = istream_dechunk_new(&GetPool(), *s, *this);
    }

    return *s;
}
