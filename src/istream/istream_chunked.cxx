/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_chunked.hxx"
#include "FacadeIstream.hxx"
#include "Bucket.hxx"
#include "util/ConstBuffer.hxx"
#include "util/Cast.hxx"
#include "util/HexFormat.h"

#include <algorithm>

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
    ChunkedIstream(struct pool &p, Istream &_input)
        :FacadeIstream(p, _input) {}

    /* virtual methods from class Istream */

    void _Read() override;
    bool _FillBucketList(IstreamBucketList &list, GError **) override;
    size_t _ConsumeBucketList(size_t nbytes) override;
    void _Close() override;

    /* virtual methods from class IstreamHandler */
    size_t OnData(const void *data, size_t length) override;
    void OnEof() override;
    void OnError(GError *error) override;

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

    ConstBuffer<void> ReadBuffer() {
        return { buffer + buffer_sent, sizeof(buffer) - buffer_sent };
    }

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

    const auto old = ReadBuffer();

#ifndef NDEBUG
    /* simulate a buffer reset; if we don't do this, an assertion in
       SetBuffer() fails (which is invalid for this special case) */
    buffer_sent = sizeof(buffer);
#endif

    auto dest = SetBuffer(old.size + length);
    memmove(dest, old.data, old.size);
    dest += old.size;

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
    auto r = ReadBuffer();
    if (r.IsEmpty())
        return true;

    size_t nbytes = InvokeData(r.data, r.size);
    if (nbytes > 0)
        buffer_sent += nbytes;

    return nbytes == r.size;
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
ChunkedIstream::_Read()
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

bool
ChunkedIstream::_FillBucketList(IstreamBucketList &list, GError **error_r)
{
    auto b = ReadBuffer();
    if (b.IsEmpty() && missing_from_current_chunk == 0) {
        off_t available = input.GetAvailable(true);
        if (available > 0) {
            StartChunk(available);
            b = ReadBuffer();
        }
    }

    if (!b.IsEmpty())
        list.Push(b);

    if (missing_from_current_chunk > 0) {
        assert(input.IsDefined());

        IstreamBucketList sub;
        if (!input.FillBucketList(sub, error_r)) {
            Destroy();
            return false;
        }

        list.SpliceBuffersFrom(sub, missing_from_current_chunk);
    }

    list.SetMore();
    return true;
}

size_t
ChunkedIstream::_ConsumeBucketList(size_t nbytes)
{
    size_t total = 0;

    size_t size = ReadBuffer().size;
    if (size > nbytes)
        size = nbytes;
    if (size > 0) {
        buffer_sent += size;
        Consumed(size);
        nbytes -= size;
        total += size;
    }

    size = std::min(nbytes, missing_from_current_chunk);
    if (size > 0) {
        assert(input.IsDefined());

        size = input.ConsumeBucketList(size);
        Consumed(size);
        nbytes -= size;
        total += size;

        missing_from_current_chunk -= size;
        if (missing_from_current_chunk == 0) {
            /* a chunk ends with "\r\n" */
            char *p = SetBuffer(2);
            p[0] = '\r';
            p[1] = '\n';
        }
    }

    return total;
}

void
ChunkedIstream::_Close()
{
    if (input.IsDefined())
        input.ClearAndClose();

    Destroy();
}

/*
 * constructor
 *
 */

Istream *
istream_chunked_new(struct pool &pool, Istream &input)
{
    return NewIstream<ChunkedIstream>(pool, input);
}
