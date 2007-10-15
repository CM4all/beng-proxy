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
        SIZE,
        AFTER_SIZE,
        DATA,
        AFTER_DATA
    } state;
    size_t size;

    void (*eof_callback)(void *ctx);
    void *ctx;
};


static void
dechunk_close(struct istream_dechunk *dechunk)
{
    if (dechunk->input != NULL)
        istream_free_unref(&dechunk->input);
    
    istream_invoke_free(&dechunk->output);
}


static void
dechunk_eof_detected(struct istream_dechunk *dechunk)
{
    assert(dechunk->input != NULL);
    assert(dechunk->state == AFTER_SIZE);
    assert(dechunk->size == 0);

    istream_clear_unref(&dechunk->input);

    pool_ref(dechunk->output.pool);
    istream_invoke_eof(&dechunk->output);
    dechunk_close(dechunk);

    if (dechunk->eof_callback != NULL)
        dechunk->eof_callback(dechunk->ctx);

    pool_unref(dechunk->output.pool);
}

static size_t
dechunk_input_data(const void *data0, size_t length, void *ctx)
{
    struct istream_dechunk *dechunk = ctx;
    const char *data = data0;
    size_t position = 0, digit, size, nbytes;

    assert(dechunk->input != NULL);

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
                return position;
            }

            if (dechunk->state == NONE) {
                dechunk->state = SIZE;
                dechunk->size = 0;
            }

            ++position;
            dechunk->size = dechunk->size * 0x10 + digit;
            break;

        case AFTER_SIZE:
            if (data[position++] == '\n') {
                if (dechunk->size == 0) {
                    dechunk_eof_detected(dechunk);
                    return position + 1;
                }

                dechunk->state = DATA;
            }
            break;

        case DATA:
            assert(dechunk->size > 0);

            size = length - position;
            if (size > dechunk->size)
                size = dechunk->size;
            nbytes = istream_invoke_data(&dechunk->output, data + position, size);
            assert(nbytes <= size);

            if (nbytes == 0)
                return position;

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
                return position;
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

    (void)dechunk;
}

static void
dechunk_input_free(void *ctx)
{
    struct istream_dechunk *dechunk = ctx;

    if (dechunk->input != NULL)
        istream_clear_unref(&dechunk->input);

    dechunk_close(dechunk);
}

static const struct istream_handler dechunk_input_handler = {
    .data = dechunk_input_data,
    .eof = dechunk_input_eof,
    .free = dechunk_input_free,
};


static inline struct istream_dechunk *
istream_to_dechunk(istream_t istream)
{
    return (struct istream_dechunk *)(((char*)istream) - offsetof(struct istream_dechunk, output));
}

static void
istream_dechunk_read(istream_t istream)
{
    struct istream_dechunk *dechunk = istream_to_dechunk(istream);

    istream_read(dechunk->input);
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
