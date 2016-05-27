/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_body.hxx"
#include "istream/istream_dechunk.hxx"
#include "buffered_socket.hxx"

#include <limits.h>

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
HttpBodyReader::SocketEOF(size_t remaining)
{
    if (rest == REST_UNKNOWN) {
        rest = remaining;

        if (remaining > 0) {
            /* serve the rest of the buffer, then end the body
               stream */
            return true;
        }

        /* the socket is closed, which ends the body */
        InvokeEof();
        return false;
    } else if (rest == REST_CHUNKED ||
               rest == REST_EOF_CHUNK) {
        /* suppress InvokeEof() because the dechunker is responsible
           for that */
        return remaining > 0;
    } else if (rest == (off_t)remaining) {
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

bool
HttpBodyReader::OnDechunkEnd()

{
    assert(rest == REST_CHUNKED);

    end_seen = true;
    rest = REST_EOF_CHUNK;

    return true;
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
