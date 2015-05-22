/*
 * This istream filter removes HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_dechunk.hxx"
#include "istream_internal.hxx"
#include "istream_pointer.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"

#include <algorithm>

#include <assert.h>
#include <string.h>

enum istream_dechunk_state {
    NONE,
    CLOSED,
    SIZE,
    AFTER_SIZE,
    DATA,
    AFTER_DATA,
    TRAILER,
    TRAILER_DATA,
    EOF_DETECTED
};

struct DechunkIstream {
    struct istream output;

    IstreamPointer input;

    enum istream_dechunk_state state;

    size_t size;
    bool had_input, had_output;

    /**
     * Copy chunked data verbatim to handler?
     *
     * @see istream_dechunk_check_verbatim()
     */
    bool verbatim = false;

    /**
     * Was the end-of-file chunk seen at the end of #pending_verbatim?
     */
    bool eof_verbatim;

    /**
     * Number of bytes to be passed to handler verbatim, which have
     * already been parsed but have not yet been consumed by the
     * handler.
     */
    size_t pending_verbatim;

    void (*const eof_callback)(void *ctx);
    void *const callback_ctx;

    DechunkIstream(struct pool &p, struct istream &_input,
                   void (*_eof_callback)(void *ctx), void *_callback_ctx);
};

static GQuark
dechunk_quark(void)
{
    return g_quark_from_static_string("dechunk");
}

static void
dechunk_abort(DechunkIstream *dechunk, GError *error)
{
    assert(dechunk->state != EOF_DETECTED && dechunk->state != CLOSED);
    assert(dechunk->input.IsDefined());

    dechunk->state = CLOSED;

    dechunk->input.ClearAndClose();
    istream_deinit_abort(&dechunk->output, error);
}

/**
 * @return false if the istream_dechunk has been aborted indirectly
 * (by a callback)
 */
static bool
dechunk_eof_detected(DechunkIstream *dechunk)
{
    assert(dechunk->input.IsDefined());
    assert(dechunk->state == TRAILER);
    assert(dechunk->size == 0);

    dechunk->state = EOF_DETECTED;

    dechunk->eof_callback(dechunk->callback_ctx);

    assert(dechunk->input.IsDefined());
    assert(dechunk->state == EOF_DETECTED);

    pool_ref(dechunk->output.pool);
    istream_deinit_eof(&dechunk->output);

    if (dechunk->state == CLOSED) {
        assert(!dechunk->input.IsDefined());

        pool_unref(dechunk->output.pool);
        return false;
    } else {
        /* we must deinitialize the "input" after emitting "eof",
           because we must give the callback a chance to call
           dechunk_input_abort() on us; if we'd clear the handler too
           early, we wouldn't receive that event, and
           dechunk_input_data() couldn't change its return value to
           0 */
        assert(dechunk->input.IsDefined());

        dechunk->input.ClearHandler();
        pool_unref(dechunk->output.pool);
        return true;
    }
}

static size_t
dechunk_feed(DechunkIstream *dechunk, const void *data0, size_t length)
{
    const char *data = (const char *)data0;
    size_t position = 0, digit, size, nbytes;

    assert(dechunk->input.IsDefined());
    assert(!dechunk->verbatim || !dechunk->eof_verbatim);

    if (dechunk->verbatim)
        /* skip the part that has already been parsed in the last
           invocation, but could not be consumed by the handler */
        position = dechunk->pending_verbatim;

    dechunk->had_input = true;

    while (position < length) {
        switch (dechunk->state) {
        case NONE:
        case SIZE:
            if (data[position] >= '0' && data[position] <= '9') {
                digit = data[position] - '0';
            } else if (data[position] >= 'a' && data[position] <= 'f') {
                digit = data[position] - 'a' + 0xa;
            } else if (data[position] >= 'A' && data[position] <= 'F') {
                digit = data[position] - 'A' + 0xa;
            } else if (dechunk->state == SIZE) {
                dechunk->state = AFTER_SIZE;
                ++position;
                continue;
            } else {
                GError *error =
                    g_error_new_literal(dechunk_quark(), 0,
                                        "chunk length expected");
                dechunk_abort(dechunk, error);
                return 0;
            }

            if (dechunk->state == NONE) {
                dechunk->state = SIZE;
                dechunk->size = 0;
            }

            ++position;
            dechunk->size = dechunk->size * 0x10 + digit;
            break;

        case CLOSED:
            assert(0);
            break;

        case AFTER_SIZE:
            if (data[position++] == '\n') {
                if (dechunk->size == 0)
                    dechunk->state = TRAILER;
                else
                    dechunk->state = DATA;
            }
            break;

        case DATA:
            assert(dechunk->size > 0);

            size = length - position;
            if (size > dechunk->size)
                size = dechunk->size;

            if (dechunk->verbatim) {
                /* postpone this data chunk; try to send it all later
                   in one big block */
                nbytes = size;
            } else {
                dechunk->had_output = true;
                nbytes = istream_invoke_data(&dechunk->output,
                                             data + position, size);
                assert(nbytes <= size);

                if (nbytes == 0)
                    return dechunk->state == CLOSED ? 0 : position;
            }

            dechunk->size -= nbytes;
            if (dechunk->size == 0)
                dechunk->state = AFTER_DATA;

            position += nbytes;
            break;

        case AFTER_DATA:
            if (data[position] == '\n') {
                dechunk->state = NONE;
            } else if (data[position] != '\r') {
                GError *error =
                    g_error_new_literal(dechunk_quark(), 0,
                                        "newline expected");
                dechunk_abort(dechunk, error);
                return 0;
            }
            ++position;
            break;

        case TRAILER:
            if (data[position] == '\n') {
                ++position;

                if (dechunk->verbatim) {
                    /* in "verbatim" mode, we need to send all data
                       before handling the EOF chunk */
                    dechunk->had_output = true;
                    nbytes = istream_invoke_data(&dechunk->output,
                                                 data, position);
                    if (dechunk->state == CLOSED)
                        return 0;

                    dechunk->pending_verbatim = position - nbytes;
                    if (dechunk->pending_verbatim > 0) {
                        /* not everything could be sent; postpone to
                           next call */
                        dechunk->eof_verbatim = true;
                        return nbytes;
                    }
                }

                return dechunk_eof_detected(dechunk) ? position : 0;
            } else if (data[position] == '\r') {
                ++position;
            } else {
                ++position;
                dechunk->state = TRAILER_DATA;
            }
            break;

        case TRAILER_DATA:
            if (data[position++] == '\n')
                dechunk->state = TRAILER;
            break;

        case EOF_DETECTED:
            assert(0);
            return 0;
        }
    }

    if (dechunk->verbatim && position > 0) {
        /* send all chunks in one big block */
        dechunk->had_output = true;
        nbytes = istream_invoke_data(&dechunk->output, data, position);
        if (dechunk->state == CLOSED)
            return 0;

        /* postpone the rest that was not handled; it will not be
           parsed again */
        dechunk->pending_verbatim = position - nbytes;
        return nbytes;
    }

    return position;
}


/*
 * istream handler
 *
 */

static size_t
dechunk_input_data(const void *data, size_t length, void *ctx)
{
    DechunkIstream *dechunk = (DechunkIstream *)ctx;

    assert(!dechunk->verbatim || length >= dechunk->pending_verbatim);

    if (dechunk->verbatim && dechunk->eof_verbatim) {
        /* during the last call, the EOF chunk was parsed, but we
           could not handle it yet, because the handler did not
           consume all data yet; try to send the remaining pre-EOF
           data again and then handle the EOF chunk */

        assert(dechunk->pending_verbatim > 0);

        assert(length >= dechunk->pending_verbatim);

        dechunk->had_output = true;
        size_t nbytes = istream_invoke_data(&dechunk->output,
                                            data, dechunk->pending_verbatim);
        if (nbytes == 0)
            return 0;

        dechunk->pending_verbatim -= nbytes;
        if (dechunk->pending_verbatim > 0)
            /* more data needed */
            return nbytes;

        return dechunk_eof_detected(dechunk) ? nbytes : 0;
    }

    const ScopePoolRef ref(*dechunk->output.pool TRACE_ARGS);
    return dechunk_feed(dechunk, data, length);
}

static void
dechunk_input_eof(void *ctx)
{
    DechunkIstream *dechunk = (DechunkIstream *)ctx;

    assert(dechunk->state != EOF_DETECTED && dechunk->state != CLOSED);

    dechunk->state = CLOSED;

    dechunk->input.Clear();

    GError *error =
        g_error_new_literal(dechunk_quark(), 0,
                            "premature EOF in dechunker");
    istream_deinit_abort(&dechunk->output, error);
}

static void
dechunk_input_abort(GError *error, void *ctx)
{
    DechunkIstream *dechunk = (DechunkIstream *)ctx;

    dechunk->input.Clear();

    if (dechunk->state != EOF_DETECTED)
        istream_deinit_abort(&dechunk->output, error);
    else
        g_error_free(error);

    dechunk->state = CLOSED;
}

static const struct istream_handler dechunk_input_handler = {
    .data = dechunk_input_data,
    .eof = dechunk_input_eof,
    .abort = dechunk_input_abort,
};


/*
 * istream implementation
 *
 */

static inline DechunkIstream *
istream_to_dechunk(struct istream *istream)
{
    return &ContainerCast2(*istream, &DechunkIstream::output);
}

static off_t
istream_dechunk_available(struct istream *istream, bool partial)
{
    DechunkIstream *dechunk = istream_to_dechunk(istream);

    if (partial && dechunk->state == DATA)
        return (off_t)dechunk->size;

    return (off_t)-1;
}

static void
istream_dechunk_read(struct istream *istream)
{
    DechunkIstream *dechunk = istream_to_dechunk(istream);

    pool_ref(dechunk->output.pool);

    dechunk->had_output = false;

    do {
        dechunk->had_input = false;
        dechunk->input.Read();
    } while (dechunk->input.IsDefined() && dechunk->had_input &&
             !dechunk->had_output);

    pool_unref(dechunk->output.pool);
}

static void
istream_dechunk_close(struct istream *istream)
{
    DechunkIstream *dechunk = istream_to_dechunk(istream);

    assert(dechunk->state != EOF_DETECTED);

    dechunk->state = CLOSED;

    dechunk->input.ClearHandlerAndClose();
    istream_deinit(&dechunk->output);
}

static const struct istream_class istream_dechunk = {
    .available = istream_dechunk_available,
    .read = istream_dechunk_read,
    .close = istream_dechunk_close,
};


/*
 * constructor
 *
 */

inline DechunkIstream::DechunkIstream(struct pool &p, struct istream &_input,
                                      void (*_eof_callback)(void *ctx),
                                      void *_callback_ctx)
    :input(_input, dechunk_input_handler, this),
     state(NONE),
     eof_callback(_eof_callback), callback_ctx(_callback_ctx)
{
    istream_init(&output, &istream_dechunk, &p);
}

struct istream *
istream_dechunk_new(struct pool *pool, struct istream *input,
                    void (*eof_callback)(void *ctx), void *callback_ctx)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));
    assert(eof_callback != nullptr);

    auto *dechunk = NewFromPool<DechunkIstream>(*pool, *pool, *input,
                                                eof_callback, callback_ctx);
    return &dechunk->output;
}

bool
istream_dechunk_check_verbatim(struct istream *i)
{
    if (i->cls != &istream_dechunk)
        /* not an istream_dechunk instance */
        return false;

    auto *dechunk = istream_to_dechunk(i);
    dechunk->verbatim = true;
    dechunk->eof_verbatim = false;
    dechunk->pending_verbatim = 0;
    return true;
}
