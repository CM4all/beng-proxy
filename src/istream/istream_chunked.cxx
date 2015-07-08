/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_chunked.hxx"
#include "istream_pointer.hxx"
#include "istream_internal.hxx"
#include "pool.hxx"
#include "format.h"
#include "util/Cast.hxx"

#include <assert.h>
#include <string.h>

struct ChunkedIstream {
    struct istream output;
    IstreamPointer input;

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

    ChunkedIstream(struct pool &p, struct istream &_input);

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

    size_t nbytes = istream_invoke_data(&output, buffer + buffer_sent, length);
    if (nbytes > 0)
        buffer_sent += nbytes;

    return nbytes == length;
}

bool
ChunkedIstream::SendBuffer2()
{
    const ScopePoolRef ref(*output.pool TRACE_ARGS);

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

        nbytes = istream_invoke_data(&output, data + total, rest);
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

static size_t
chunked_input_data(const void *data, size_t length, void *ctx)
{
    auto *chunked = (ChunkedIstream *)ctx;

    if (chunked->writing_buffer)
        /* this is a recursive call from istream_chunked_read(): bail
           out */
        return 0;

    const ScopePoolRef ref(*chunked->output.pool TRACE_ARGS);
    return chunked->Feed((const char*)data, length);
}

static void
chunked_input_eof(void *ctx)
{
    auto *chunked = (ChunkedIstream *)ctx;

    assert(chunked->input.IsDefined());
    assert(chunked->missing_from_current_chunk == 0);

    chunked->input.Clear();

    /* write EOF chunk (length 0) */

    chunked->AppendToBuffer("0\r\n\r\n", 5);

    /* flush the buffer */

    if (chunked->SendBuffer())
        istream_deinit_eof(&chunked->output);
}

static void
chunked_input_abort(GError *error, void *ctx)
{
    auto *chunked = (ChunkedIstream *)ctx;

    assert(chunked->input.IsDefined());

    chunked->input.Clear();

    istream_deinit_abort(&chunked->output, error);
}

static constexpr struct istream_handler chunked_input_handler = {
    .data = chunked_input_data,
    .eof = chunked_input_eof,
    .abort = chunked_input_abort,
};


/*
 * istream implementation
 *
 */

static inline ChunkedIstream *
istream_to_chunked(struct istream *istream)
{
    return &ContainerCast2(*istream, &ChunkedIstream::output);
}

static void
istream_chunked_read(struct istream *istream)
{
    ChunkedIstream *chunked = istream_to_chunked(istream);

    if (!chunked->SendBuffer2())
        return;

    if (!chunked->input.IsDefined()) {
        istream_deinit_eof(&chunked->output);
        return;
    }

    if (chunked->IsBufferEmpty() &&
        chunked->missing_from_current_chunk == 0) {
        off_t available = chunked->input.GetAvailable(true);
        if (available > 0) {
            chunked->StartChunk(available);
            if (!chunked->SendBuffer2())
                return;
        }
    }

    chunked->input.Read();
}

static void
istream_chunked_close(struct istream *istream)
{
    ChunkedIstream *chunked = istream_to_chunked(istream);

    if (chunked->input.IsDefined())
        chunked->input.ClearAndClose();

    istream_deinit(&chunked->output);
}

static constexpr struct istream_class istream_chunked = {
    .read = istream_chunked_read,
    .close = istream_chunked_close,
};


/*
 * constructor
 *
 */

inline ChunkedIstream::ChunkedIstream(struct pool &p, struct istream &_input)
    :input(_input, chunked_input_handler, this)
{
    istream_init(&output, &istream_chunked, &p);
}

struct istream *
istream_chunked_new(struct pool *pool, struct istream *input)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto *chunked = NewFromPool<ChunkedIstream>(*pool, *pool, *input);
    return &chunked->output;
}
