/*
 * This istream filter removes HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
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
        AFTER_DATA,
        TRAILER,
        TRAILER_DATA,
        EOF_DETECTED
    } state;
    size_t size;
    bool had_input:1, had_output:1;
};


static void
dechunk_close(struct istream_dechunk *dechunk)
{
    assert(dechunk->state != EOF_DETECTED && dechunk->state != CLOSED);

    if (dechunk->input != NULL)
        istream_free_handler(&dechunk->input);

    dechunk->state = CLOSED;
    istream_deinit_abort(&dechunk->output);
}


static bool
dechunk_eof_detected(struct istream_dechunk *dechunk)
{
    assert(dechunk->input != NULL);
    assert(dechunk->state == TRAILER);
    assert(dechunk->size == 0);

    dechunk->state = EOF_DETECTED;

    pool_ref(dechunk->output.pool);
    istream_deinit_eof(&dechunk->output);

    if (dechunk->state == CLOSED) {
        pool_unref(dechunk->output.pool);
        return true;
    } else {
        istream_handler_clear(dechunk->input);
        dechunk->input = NULL;
        pool_unref(dechunk->output.pool);
        return false;
    }
}

static size_t
dechunk_feed(struct istream_dechunk *dechunk, const void *data0, size_t length)
{
    const char *data = data0;
    size_t position = 0, digit, size, nbytes;

    assert(dechunk->input != NULL);

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
            dechunk->had_output = true;
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

        case TRAILER:
            if (data[position] == '\n') {
                return dechunk_eof_detected(dechunk) ? 0 : position + 1;
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

    return position;
}


/*
 * istream handler
 *
 */

static size_t
dechunk_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_dechunk *dechunk = ctx;
    size_t nbytes;

    pool_ref(dechunk->output.pool);
    nbytes = dechunk_feed(dechunk, data, length);
    pool_unref(dechunk->output.pool);

    return nbytes;
}

static void
dechunk_input_eof(void *ctx)
{
    struct istream_dechunk *dechunk = ctx;

    assert(dechunk->state != EOF_DETECTED && dechunk->state != CLOSED);

    daemon_log(2, "premature EOF in dechunker\n");

    dechunk->state = CLOSED;

    dechunk->input = NULL;
    istream_deinit_abort(&dechunk->output);
}

static void
dechunk_input_abort(void *ctx)
{
    struct istream_dechunk *dechunk = ctx;

    assert(dechunk->state != CLOSED);

    dechunk->input = NULL;

    if (dechunk->state != EOF_DETECTED)
        istream_deinit_abort(&dechunk->output);

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

static inline struct istream_dechunk *
istream_to_dechunk(istream_t istream)
{
    return (struct istream_dechunk *)(((char*)istream) - offsetof(struct istream_dechunk, output));
}

static off_t 
istream_dechunk_available(istream_t istream, bool partial)
{
    struct istream_dechunk *dechunk = istream_to_dechunk(istream);

    if (partial && dechunk->state == DATA)
        return (off_t)dechunk->size;

    return (off_t)-1;
}

static void
istream_dechunk_read(istream_t istream)
{
    struct istream_dechunk *dechunk = istream_to_dechunk(istream);

    pool_ref(dechunk->output.pool);

    dechunk->had_output = false;

    do {
        dechunk->had_input = false;
        istream_read(dechunk->input);
    } while (dechunk->input != NULL && dechunk->had_input &&
             !dechunk->had_output);

    pool_unref(dechunk->output.pool);
}

static void
istream_dechunk_close(istream_t istream)
{
    struct istream_dechunk *dechunk = istream_to_dechunk(istream);

    dechunk_close(dechunk);
}

static const struct istream istream_dechunk = {
    .available = istream_dechunk_available,
    .read = istream_dechunk_read,
    .close = istream_dechunk_close,
};


/*
 * constructor
 *
 */

istream_t
istream_dechunk_new(pool_t pool, istream_t input)
{
    struct istream_dechunk *dechunk = istream_new_macro(pool, dechunk);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    dechunk->state = NONE;

    istream_assign_handler(&dechunk->input, input,
                           &dechunk_input_handler, dechunk,
                           0);

    return istream_struct_cast(&dechunk->output);
}

int
istream_dechunk_eof(istream_t istream)
{
    struct istream_dechunk *dechunk = istream_to_dechunk(istream);

    return dechunk->state == EOF_DETECTED;
}
