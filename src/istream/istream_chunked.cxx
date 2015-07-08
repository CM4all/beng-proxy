/*
 * This istream filter adds HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_chunked.hxx"
#include "istream_internal.hxx"
#include "pool.hxx"
#include "format.h"
#include "util/Cast.hxx"

#include <assert.h>
#include <string.h>

struct ChunkedIstream {
    struct istream output;
    struct istream *input;

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
};

static inline int
chunked_buffer_empty(const ChunkedIstream *chunked)
{
    assert(chunked->buffer_sent <= sizeof(chunked->buffer));

    return chunked->buffer_sent == sizeof(chunked->buffer);
}

/** set the buffer length and return a pointer to the first byte */
static inline char *
chunked_buffer_set(ChunkedIstream *chunked, size_t length)
{
    assert(chunked_buffer_empty(chunked));
    assert(length <= sizeof(chunked->buffer));

    chunked->buffer_sent = sizeof(chunked->buffer) - length;
    return chunked->buffer + chunked->buffer_sent;
}

/** append data to the buffer */
static void
chunked_buffer_append(ChunkedIstream *chunked,
                      const void *data, size_t length)
{
    assert(data != nullptr);
    assert(length > 0);
    assert(length <= chunked->buffer_sent);

    const void *old = chunked->buffer + chunked->buffer_sent;
    size_t old_length = sizeof(chunked->buffer) - chunked->buffer_sent;

#ifndef NDEBUG
    /* simulate a buffer reset; if we don't do this, an assertion in
       chunked_buffer_set() fails (which is invalid for this special
       case) */
    chunked->buffer_sent = sizeof(chunked->buffer);
#endif

    auto dest = chunked_buffer_set(chunked, old_length + length);
    memmove(dest, old, old_length);
    dest += old_length;

    memcpy(dest, data, length);
}

static void
chunked_start_chunk(ChunkedIstream *chunked, size_t length)
{
    assert(length > 0);
    assert(chunked_buffer_empty(chunked));
    assert(chunked->missing_from_current_chunk == 0);

    if (length > 0x8000)
        /* maximum chunk size is 32kB for now */
        length = 0x8000;

    chunked->missing_from_current_chunk = length;

    auto buffer = chunked_buffer_set(chunked, 6);
    format_uint16_hex_fixed(buffer, (uint16_t)length);
    buffer[4] = '\r';
    buffer[5] = '\n';
}

/**
 * Returns true if the buffer is consumed.
 */
static bool
chunked_write_buffer(ChunkedIstream *chunked)
{
    size_t length = sizeof(chunked->buffer) - chunked->buffer_sent;
    if (length == 0)
        return true;

    size_t nbytes = istream_invoke_data(&chunked->output,
                                        chunked->buffer + chunked->buffer_sent,
                                        length);
    if (nbytes > 0)
        chunked->buffer_sent += nbytes;

    return nbytes == length;
}

/**
 * Wrapper for chunked_write_buffer() that sets and clears the
 * writing_buffer flag.  This requires acquiring a pool reference to
 * do that safely.
 *
 * @return true if the buffer is consumed.
 */
static bool
chunked_write_buffer2(ChunkedIstream *chunked)
{
    const ScopePoolRef ref(*chunked->output.pool TRACE_ARGS);

    assert(!chunked->writing_buffer);
    chunked->writing_buffer = true;

    const bool result = chunked_write_buffer(chunked);
    chunked->writing_buffer = false;
    return result;
}

static size_t
chunked_feed(ChunkedIstream *chunked, const char *data, size_t length)
{
    size_t total = 0, rest, nbytes;

    assert(chunked->input != nullptr);

    do {
        assert(!chunked->writing_buffer);

        if (chunked_buffer_empty(chunked) &&
            chunked->missing_from_current_chunk == 0)
            chunked_start_chunk(chunked, length - total);

        if (!chunked_write_buffer(chunked))
            return chunked->input == nullptr ? 0 : total;

        assert(chunked_buffer_empty(chunked));

        if (chunked->missing_from_current_chunk == 0) {
            /* we have just written the previous chunk trailer;
               re-start this loop to start a new chunk */
            nbytes = rest = 0;
            continue;
        }

        rest = length - total;
        if (rest > chunked->missing_from_current_chunk)
            rest = chunked->missing_from_current_chunk;

        nbytes = istream_invoke_data(&chunked->output, data + total, rest);
        if (nbytes == 0)
            return chunked->input == nullptr ? 0 : total;

        total += nbytes;

        chunked->missing_from_current_chunk -= nbytes;
        if (chunked->missing_from_current_chunk == 0) {
            /* a chunk ends with "\r\n" */
            char *buffer = chunked_buffer_set(chunked, 2);
            buffer[0] = '\r';
            buffer[1] = '\n';
        }
    } while ((!chunked_buffer_empty(chunked) || total < length) &&
             nbytes == rest);

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
    return chunked_feed(chunked, (const char*)data, length);
}

static void
chunked_input_eof(void *ctx)
{
    auto *chunked = (ChunkedIstream *)ctx;

    assert(chunked->input != nullptr);
    assert(chunked->missing_from_current_chunk == 0);

    chunked->input = nullptr;

    /* write EOF chunk (length 0) */

    chunked_buffer_append(chunked, "0\r\n\r\n", 5);

    /* flush the buffer */

    if (chunked_write_buffer(chunked))
        istream_deinit_eof(&chunked->output);
}

static void
chunked_input_abort(GError *error, void *ctx)
{
    auto *chunked = (ChunkedIstream *)ctx;

    assert(chunked->input != nullptr);

    chunked->input = nullptr;

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

    if (!chunked_write_buffer2(chunked))
        return;

    if (chunked->input == nullptr) {
        istream_deinit_eof(&chunked->output);
        return;
    }

    assert(chunked->input != nullptr);

    if (chunked_buffer_empty(chunked) &&
        chunked->missing_from_current_chunk == 0) {
        off_t available = istream_available(chunked->input, true);
        if (available > 0) {
            chunked_start_chunk(chunked, available);
            if (!chunked_write_buffer2(chunked))
                return;
        }
    }

    istream_read(chunked->input);
}

static void
istream_chunked_close(struct istream *istream)
{
    ChunkedIstream *chunked = istream_to_chunked(istream);

    if (chunked->input != nullptr)
        istream_free_handler(&chunked->input);

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
{
    istream_init(&output, &istream_chunked, &p);
    istream_assign_handler(&input, &_input,
                           &chunked_input_handler, this,
                           0);
}

struct istream *
istream_chunked_new(struct pool *pool, struct istream *input)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto *chunked = NewFromPool<ChunkedIstream>(*pool, *pool, *input);
    return &chunked->output;
}
