/*
 * Utilities for reading a HTTP body, either request or response.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_body.hxx"
#include "istream-internal.h"
#include "istream_dechunk.hxx"
#include "filtered_socket.hxx"

#include <assert.h>
#include <limits.h>

void
HttpBodyReader::Deinit()
{
    istream_deinit(&output);
}

void
HttpBodyReader::DeinitEOF()
{
    istream_deinit_eof(&output);
}

void
HttpBodyReader::DeinitAbort(GError *error)
{
    istream_deinit_abort(&output, error);
}

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
    size_t consumed = istream_invoke_data(&output, data, length);
    if (consumed > 0)
        Consumed(consumed);

    return consumed;
}

bool
HttpBodyReader::CheckDirect(enum istream_direct fd_type) const
{
    return istream_check_direct(&output, fd_type);
}

ssize_t
HttpBodyReader::TryDirect(int fd, enum istream_direct fd_type)
{
    assert(fd >= 0);
    assert(istream_check_direct(&output, fd_type));
    assert(output.handler->direct != nullptr);

    ssize_t nbytes = istream_invoke_direct(&output,
                                           fd_type, fd,
                                           GetMaxRead(INT_MAX));
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
#ifndef NDEBUG
    socket_eof = true;
#endif

    if (rest == REST_UNKNOWN) {
        if (remaining > 0) {
            /* serve the rest of the buffer, then end the body
               stream */
            rest = remaining;
            return true;
        }

        /* the socket is closed, which ends the body */
        istream_deinit_eof(&output);
        return false;
    } else if (rest == (off_t)remaining ||
               rest == REST_CHUNKED ||
               rest == REST_EOF_CHUNK) {
        if (remaining > 0)
            /* serve the rest of the buffer, then end the body
               stream */
            return true;

        istream_deinit_eof(&output);
        return false;
    } else {
        /* something has gone wrong: either not enough or too much
           data left in the buffer */
        GError *error = g_error_new_literal(buffered_socket_quark(), 0,
                                            "premature end of socket");
        istream_deinit_abort(&output, error);
        return false;
    }
}

void
HttpBodyReader::DechunkerEOF()
{

    assert(chunked);
    assert(rest == REST_CHUNKED);

    rest = REST_EOF_CHUNK;
}

void
HttpBodyReader::DechunkerEOF(void *ctx)
{
    HttpBodyReader *body = (HttpBodyReader *)ctx;

    body->DechunkerEOF();
}

struct istream &
HttpBodyReader::Init(const struct istream_class &stream,
                     struct pool &stream_pool,
                     struct pool &pool, off_t content_length, bool _chunked)
{
    assert(pool_contains(&stream_pool, this, sizeof(*this)));
    assert(content_length >= -1);

    istream_init(&output, &stream, &stream_pool);
    rest = content_length;

#ifndef NDEBUG
    chunked = _chunked;
    socket_eof = false;
#endif

    struct istream *istream = &output;
    if (_chunked) {
        assert(rest == (off_t)REST_UNKNOWN);

        rest = REST_CHUNKED;

        istream = istream_dechunk_new(&pool, istream,
                                      DechunkerEOF, this);
    }

    return *istream;
}
