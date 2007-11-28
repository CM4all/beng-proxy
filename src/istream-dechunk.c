/*
 * This istream filter removes HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "fifo-buffer.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>

struct istream_dechunk {
    struct istream output;
    istream_t input;
    enum {
        NONE,
        CLOSED,
        SIZE,
        AFTER_SIZE,
        DATA,
        AFTER_DATA
    } state;
    size_t size;
    unsigned had_input:1, had_output:1;

    void (*eof_callback)(void *ctx);
    void *ctx;
};


static void
dechunk_close(struct istream_dechunk *dechunk)
{
    dechunk->state = CLOSED;

    if (dechunk->input != NULL)
        istream_clear_unref_handler(&dechunk->input);
    
    istream_invoke_abort(&dechunk->output);
}


static void
dechunk_eof_detected(struct istream_dechunk *dechunk)
{
    assert(dechunk->input != NULL);
    assert(dechunk->state == AFTER_SIZE);
    assert(dechunk->size == 0);

    istream_clear_unref_handler(&dechunk->input);

    pool_ref(dechunk->output.pool);
    istream_invoke_eof(&dechunk->output);

    if (dechunk->eof_callback != NULL)
        dechunk->eof_callback(dechunk->ctx);

    pool_unref(dechunk->output.pool);
}


/*
 * istream handler
 *
 */

static size_t
dechunk_input_data(const void *data0, size_t length, void *ctx)
{
    struct istream_dechunk *dechunk = ctx;
    const char *data = data0;
    size_t position = 0, digit, size, nbytes;

    assert(dechunk->input != NULL);

    dechunk->had_input = 1;

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
                daemon_log(2, "chunk length expected\n");
                dechunk_close(dechunk);
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
                if (dechunk->size == 0) {
                    dechunk_eof_detected(dechunk);
                    return dechunk->state == CLOSED ? 0 : position;
                }

                dechunk->state = DATA;
            }
            break;

        case DATA:
            assert(dechunk->size > 0);

            size = length - position;
            if (size > dechunk->size)
                size = dechunk->size;
            dechunk->had_output = 1;
            nbytes = istream_invoke_data(&dechunk->output, data + position, size);
            assert(nbytes <= size);

            if (nbytes == 0)
                return dechunk->state == CLOSED ? 0 : position;

            dechunk->size -= nbytes;
            if (dechunk->size == 0)
                dechunk->state = AFTER_DATA;

            position += nbytes;
            break;

        case AFTER_DATA:
            if (data[position] == '\n') {
                dechunk->state = NONE;
            } else if (data[position] != '\r') {
                daemon_log(2, "newline expected\n");
                dechunk_close(dechunk);
                return 0;
            }
            ++position;
            break;
        }
    }

    return position;
}

static void
dechunk_input_eof(void *ctx)
{
    struct istream_dechunk *dechunk = ctx;

    istream_clear_unref(&dechunk->input);

    if (dechunk->state != NONE) {
        daemon_log(2, "premature EOF in dechunker");
        dechunk_close(dechunk);
    }
}

static void
dechunk_input_abort(void *ctx)
{
    struct istream_dechunk *dechunk = ctx;

    istream_clear_unref(&dechunk->input);

    dechunk_close(dechunk);
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

static inline struct istream_dechunk *
istream_to_dechunk(istream_t istream)
{
    return (struct istream_dechunk *)(((char*)istream) - offsetof(struct istream_dechunk, output));
}

static void
istream_dechunk_read(istream_t istream)
{
    struct istream_dechunk *dechunk = istream_to_dechunk(istream);

    dechunk->had_output = 0;

    do {
        dechunk->had_input = 0;
        istream_read(dechunk->input);
    } while (dechunk->input != NULL && dechunk->had_input &&
             !dechunk->had_output);
}

static void
istream_dechunk_close(istream_t istream)
{
    struct istream_dechunk *dechunk = istream_to_dechunk(istream);

    dechunk_close(dechunk);
}

static const struct istream istream_dechunk = {
    .read = istream_dechunk_read,
    .close = istream_dechunk_close,
};


/*
 * constructor
 *
 */

istream_t
istream_dechunk_new(pool_t pool, istream_t input,
                    void (*eof_callback)(void *ctx), void *ctx)
{
    struct istream_dechunk *dechunk = p_malloc(pool, sizeof(*dechunk));

    assert(input != NULL);
    assert(!istream_has_handler(input));

    dechunk->output = istream_dechunk;
    dechunk->output.pool = pool;
    dechunk->state = NONE;
    dechunk->eof_callback = eof_callback;
    dechunk->ctx = ctx;

    istream_assign_ref_handler(&dechunk->input, input,
                               &dechunk_input_handler, dechunk,
                               0);

    return istream_struct_cast(&dechunk->output);
}
