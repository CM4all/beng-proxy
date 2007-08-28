/*
 * This istream filter removes HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "fifo-buffer.h"

#include <assert.h>
#include <string.h>

struct istream_dechunk {
    struct istream output;
    istream_t input;
    enum {
        NONE,
        SIZE,
        AFTER_SIZE,
        DATA
    } state;
    size_t size;
};

static void
dechunk_eof_detected(struct istream_dechunk *dechunk)
{
    assert(dechunk->input != NULL);
    assert(dechunk->state == AFTER_SIZE);
    assert(dechunk->size == 0);

    pool_unref(dechunk->input->pool);
    dechunk->input = NULL;

    istream_invoke_eof(&dechunk->output);
    istream_close(&dechunk->output);
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
            } else {
                if (dechunk->state == SIZE)
                    dechunk->state = AFTER_SIZE;
                ++position;
                continue;
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

            dechunk->size -= nbytes;
            if (dechunk->size == 0)
                dechunk->state = NONE;

            position += nbytes;
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

    if (dechunk->input != NULL) {
        pool_unref(dechunk->input->pool);
        dechunk->input = NULL;

        istream_close(&dechunk->output);
    }
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

    if (dechunk->input != NULL) {
        pool_t pool = dechunk->input->pool;
        istream_free(&dechunk->input);
        pool_unref(pool);
    }
    
    istream_invoke_free(&dechunk->output);
}

static const struct istream istream_dechunk = {
    .read = istream_dechunk_read,
    .close = istream_dechunk_close,
};

istream_t
istream_dechunk_new(pool_t pool, istream_t input)
{
    struct istream_dechunk *dechunk = p_malloc(pool, sizeof(*dechunk));

    assert(input != NULL);
    assert(input->handler == NULL);

    dechunk->output = istream_dechunk;
    dechunk->output.pool = pool;
    dechunk->input = input;
    dechunk->state = NONE;

    input->handler = &dechunk_input_handler;
    input->handler_ctx = dechunk;
    pool_ref(input->pool);

    return &dechunk->output;
}
