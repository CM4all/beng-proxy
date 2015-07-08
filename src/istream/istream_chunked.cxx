/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_chunked.hxx"
#include "FacadeIstream.hxx"
#include "format.h"

#include <assert.h>
#include <string.h>

class ChunkedIstream final : public FacadeIstream {
    /**
     * This flag is true while writing the buffer inside
     * istream_chunked_read().  chunked_input_data() will check it,
     * and refuse to accept more data from the input.  This avoids
     * writing the buffer recursively.
     */
    bool writing_buffer = false;

    char buffer[7];
    size_t buffer_sent = sizeof(buffer);

    size_t missing_from_current_chunk = 0;

public:
    ChunkedIstream(struct pool &p, struct istream &_input)
        :FacadeIstream(p, _input,
                       MakeIstreamHandler<ChunkedIstream>::handler, this) {}

    /* virtual methods from class Istream */

    void Read() override;
    void Close() override;

    /* handler */
    size_t OnData(const void *data, size_t length);

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof();
    void OnError(GError *error);

private:
    bool IsBufferEmpty() const {
        assert(buffer_sent <= sizeof(buffer));

        return buffer_sent == sizeof(buffer);
    }

    /** set the buffer length and return a pointer to the first byte */
    char *SetBuffer(size_t length) {
        assert(IsBufferEmpty());
        assert(length <= sizeof(buffer));

        buffer_sent = sizeof(buffer) - length;
        return buffer + buffer_sent;
    }

    /** append data to the buffer */
    void AppendToBuffer(const void *data, size_t length);

    void StartChunk(size_t length);

    /**
     * Returns true if the buffer is consumed.
     */
    bool SendBuffer();

    /**
     * Wrapper for SendBuffer() that sets and clears the
     * writing_buffer flag.  This requires acquiring a pool reference
     * to do that safely.
     *
     * @return true if the buffer is consumed.
     */
    bool SendBuffer2();

    size_t Feed(const char *data, size_t length);
};

void
ChunkedIstream::AppendToBuffer(const void *data, size_t length)
{
    assert(data != nullptr);
    assert(length > 0);
    assert(length <= buffer_sent);

    const void *old = buffer + buffer_sent;
    size_t old_length = sizeof(buffer) - buffer_sent;

#ifndef NDEBUG
    /* simulate a buffer reset; if we don't do this, an assertion in
       chunked_buffer_set() fails (which is invalid for this special
       case) */
    buffer_sent = sizeof(buffer);
#endif

    auto dest = SetBuffer(old_length + length);
    memmove(dest, old, old_length);
    dest += old_length;

    memcpy(dest, data, length);
}

void
ChunkedIstream::StartChunk(size_t length)
{
    assert(length > 0);
    assert(IsBufferEmpty());
    assert(missing_from_current_chunk == 0);

    if (length > 0x8000)
        /* maximum chunk size is 32kB for now */
        length = 0x8000;

    missing_from_current_chunk = length;

    auto p = SetBuffer(6);
    format_uint16_hex_fixed(p, (uint16_t)length);
    p[4] = '\r';
    p[5] = '\n';
}

bool
ChunkedIstream::SendBuffer()
{
    size_t length = sizeof(buffer) - buffer_sent;
    if (length == 0)
        return true;

    size_t nbytes = InvokeData(buffer + buffer_sent, length);
    if (nbytes > 0)
        buffer_sent += nbytes;

    return nbytes == length;
}

bool
ChunkedIstream::SendBuffer2()
{
    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    assert(!writing_buffer);
    writing_buffer = true;

    const bool result = SendBuffer();
    writing_buffer = false;
    return result;
}

inline size_t
ChunkedIstream::Feed(const char *data, size_t length)
{
    size_t total = 0, rest, nbytes;

    assert(input.IsDefined());

    do {
        assert(!writing_buffer);

        if (IsBufferEmpty() && missing_from_current_chunk == 0)
            StartChunk(length - total);

        if (!SendBuffer())
            return input.IsDefined() ? total : 0;

        assert(IsBufferEmpty());

        if (missing_from_current_chunk == 0) {
            /* we have just written the previous chunk trailer;
               re-start this loop to start a new chunk */
            nbytes = rest = 0;
            continue;
        }

        rest = length - total;
        if (rest > missing_from_current_chunk)
            rest = missing_from_current_chunk;

        nbytes = InvokeData(data + total, rest);
        if (nbytes == 0)
            return input.IsDefined() ? total : 0;

        total += nbytes;

        missing_from_current_chunk -= nbytes;
        if (missing_from_current_chunk == 0) {
            /* a chunk ends with "\r\n" */
            char *p = SetBuffer(2);
            p[0] = '\r';
            p[1] = '\n';
        }
    } while ((!IsBufferEmpty() || total < length) && nbytes == rest);

    return total;
}


/*
 * istream handler
 *
 */

size_t
ChunkedIstream::OnData(const void *data, size_t length)
{
    if (writing_buffer)
        /* this is a recursive call from istream_chunked_read(): bail
           out */
        return 0;

    const ScopePoolRef ref(GetPool() TRACE_ARGS);
    return Feed((const char*)data, length);
}

void
ChunkedIstream::OnEof()
{
    assert(input.IsDefined());
    assert(missing_from_current_chunk == 0);

    input.Clear();

    /* write EOF chunk (length 0) */

    AppendToBuffer("0\r\n\r\n", 5);

    /* flush the buffer */

    if (SendBuffer())
        DestroyEof();
}

void
ChunkedIstream::OnError(GError *error)
{
    assert(input.IsDefined());

    input.Clear();
    DestroyError(error);
}

/*
 * istream implementation
 *
 */

void
ChunkedIstream::Read()
{
    if (!SendBuffer2())
        return;

    if (!input.IsDefined()) {
        DestroyEof();
        return;
    }

    if (IsBufferEmpty() && missing_from_current_chunk == 0) {
        off_t available = input.GetAvailable(true);
        if (available > 0) {
            StartChunk(available);
            if (!SendBuffer2())
                return;
        }
    }

    input.Read();
}

void
ChunkedIstream::Close()
{
    if (input.IsDefined())
        input.ClearAndClose();

    Destroy();
}

/*
 * constructor
 *
 */

struct istream *
istream_chunked_new(struct pool *pool, struct istream *input)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    return NewIstream<ChunkedIstream>(*pool, *input);
}
