/*
 * This istream filter removes HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream.h"
#include "fifo-buffer.h"

#include <daemon/log.h>

#include <assert.h>
#include <zlib.h>

struct istream_deflate {
    struct istream output;
    istream_t input;
    fifo_buffer_t buffer;
    int z_initialized;
    z_stream z;
};


static void
deflate_close(struct istream_deflate *defl)
{
    if (defl->input != NULL)
        istream_free_unref(&defl->input);

    if (defl->z_initialized) {
        defl->z_initialized = 0;
        deflateEnd(&defl->z);
    }

    istream_invoke_free(&defl->output);
}

static voidpf z_alloc
(voidpf opaque, uInt items, uInt size)
{
    pool_t pool = opaque;

    return p_malloc(pool, items * size);
}

static void
z_free(voidpf opaque, voidpf address)
{
    (void)opaque;
    (void)address;
}

static int
deflate_initialize_z(struct istream_deflate *defl)
{
    int err;

    if (defl->z_initialized)
        return Z_OK;

    defl->z.zalloc = z_alloc;
    defl->z.zfree = z_free;
    defl->z.opaque = defl->output.pool;

    err = deflateInit(&defl->z, Z_DEFAULT_COMPRESSION);
    if (err != Z_OK) {
        daemon_log(2, "deflateInit(Z_FINISH) failed: %d\n", err);
        deflate_close(defl);
        return err;
    }

    defl->z_initialized = 1;
    return Z_OK;
}

static size_t
deflate_try_write(struct istream_deflate *defl)
{
    const void *data;
    size_t length, nbytes;

    assert(!fifo_buffer_empty(defl->buffer));

    data = fifo_buffer_read(defl->buffer, &length);
    nbytes = istream_invoke_data(&defl->output, data, length);
    if (nbytes > 0) {
        fifo_buffer_consume(defl->buffer, nbytes);

        if (nbytes == length && defl->input == NULL)
            istream_invoke_eof(&defl->output);
    }

    return nbytes;
}

static void *
deflate_buffer_write(struct istream_deflate *defl, size_t *max_length_r)
{
    void *data;
    size_t nbytes;

    data = fifo_buffer_write(defl->buffer, max_length_r);
    if (data != NULL)
        return data;

    nbytes = deflate_try_write(defl);
    if (nbytes == 0)
        return NULL;

    return fifo_buffer_write(defl->buffer, max_length_r);
}


/*
 * istream handler
 *
 */

static size_t
deflate_input_data(const void *data, size_t length, void *ctx)
{
    struct istream_deflate *defl = ctx;
    void *dest_buffer;
    size_t max_length, nbytes;
    union {
        const void *input;
        Bytef *output;
    } deconst;
    int err;

    assert(defl->input != NULL);

    dest_buffer = deflate_buffer_write(defl, &max_length);
    if (dest_buffer == NULL || max_length < 64) /* reserve space for end-of-stream marker */
        return 0;

    err = deflate_initialize_z(defl);
    if (err != Z_OK)
        return 0;

    defl->z.next_out = dest_buffer;
    defl->z.avail_out = (uInt)max_length;

    deconst.input = data;
    defl->z.next_in = deconst.output;
    defl->z.avail_in = (uInt)length;

    err = deflate(&defl->z, Z_NO_FLUSH);
    if (err != Z_OK) {
        daemon_log(2, "deflate() failed: %d\n", err);
        deflate_close(defl);
        return 0;
    }

    nbytes = max_length - (size_t)defl->z.avail_out;
    if (nbytes > 0) {
        fifo_buffer_append(defl->buffer, nbytes);
        deflate_try_write(defl);
    }

    return length - (size_t)defl->z.avail_in;
}

static void
deflate_input_eof(void *ctx)
{
    struct istream_deflate *defl = ctx;
    void *dest_buffer;
    size_t max_length;
    int err;

    assert(defl->input != NULL);
    istream_clear_unref_handler(&defl->input);

    dest_buffer = deflate_buffer_write(defl, &max_length);
    assert(dest_buffer != NULL);

    err = deflate_initialize_z(defl);
    if (err != Z_OK)
        return;

    defl->z.next_out = dest_buffer;
    defl->z.avail_out = (uInt)max_length;

    defl->z.next_in = NULL;
    defl->z.avail_in = 0;

    err = deflate(&defl->z, Z_FINISH);
    if (err != Z_STREAM_END) {
        daemon_log(2, "deflate(Z_FINISH) failed: %d\n", err);
        deflate_close(defl);
        return;
    }

    fifo_buffer_append(defl->buffer, max_length - (size_t)defl->z.avail_out);

    deflate_try_write(defl);
}

static void
deflate_input_free(void *ctx)
{
    struct istream_deflate *defl = ctx;

    if (defl->input != NULL) {
        istream_clear_unref(&defl->input);

        deflate_close(defl);
    }
}

static const struct istream_handler deflate_input_handler = {
    .data = deflate_input_data,
    .eof = deflate_input_eof,
    .free = deflate_input_free,
};


/*
 * istream implementation
 *
 */

static inline struct istream_deflate *
istream_to_deflate(istream_t istream)
{
    return (struct istream_deflate *)(((char*)istream) - offsetof(struct istream_deflate, output));
}

static void
istream_deflate_read(istream_t istream)
{
    struct istream_deflate *defl = istream_to_deflate(istream);

    if (fifo_buffer_empty(defl->buffer))
        istream_read(defl->input);
    else
        deflate_try_write(defl);
}

static void
istream_deflate_close(istream_t istream)
{
    struct istream_deflate *defl = istream_to_deflate(istream);

    deflate_close(defl);
}

static const struct istream istream_deflate = {
    .read = istream_deflate_read,
    .close = istream_deflate_close,
};


/*
 * constructor
 *
 */

istream_t
istream_deflate_new(pool_t pool, istream_t input)
{
    struct istream_deflate *defl = p_malloc(pool, sizeof(*defl));

    assert(input != NULL);
    assert(!istream_has_handler(input));

    defl->output = istream_deflate;
    defl->output.pool = pool;
    defl->buffer = fifo_buffer_new(pool, 4096);
    defl->z_initialized = 0;

    istream_assign_ref_handler(&defl->input, input,
                               &deflate_input_handler, defl,
                               0);

    return istream_struct_cast(&defl->output);
}
