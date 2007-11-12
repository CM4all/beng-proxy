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
    int z_initialized, z_stream_end;
    z_stream z;
    unsigned had_input:1, had_output:1;
};


static void
deflate_close(struct istream_deflate *defl)
{
    if (defl->z_initialized) {
        defl->z_initialized = 0;
        deflateEnd(&defl->z);
    }
}

static void
deflate_abort(struct istream_deflate *defl)
{
    deflate_close(defl);

    if (defl->input != NULL)
        istream_free_unref_handler(&defl->input);

    istream_invoke_abort(&defl->output);
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

    pool_ref(defl->output.pool);
    nbytes = istream_invoke_data(&defl->output, data, length);
    if (pool_unref(defl->output.pool) == 0)
        return 0;

    if (!defl->z_initialized)
        return 0;

    if (nbytes > 0) {
        fifo_buffer_consume(defl->buffer, nbytes);

        if (nbytes == length && defl->input == NULL && defl->z_stream_end) {
            deflate_close(defl);
            istream_invoke_eof(&defl->output);
        }
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

static void
deflate_try_flush(struct istream_deflate *defl)
{
    void *dest_buffer;
    size_t max_length;
    int err;

    assert(!defl->z_stream_end);

    dest_buffer = deflate_buffer_write(defl, &max_length);
    if (dest_buffer == NULL)
        return;

    defl->z.next_out = dest_buffer;
    defl->z.avail_out = (uInt)max_length;

    defl->z.next_in = NULL;
    defl->z.avail_in = 0;

    err = deflate(&defl->z, Z_SYNC_FLUSH);
    if (err != Z_OK) {
        daemon_log(2, "deflate(Z_SYNC_FLUSH) failed: %d\n", err);
        deflate_close(defl);
        return;
    }

    fifo_buffer_append(defl->buffer, max_length - (size_t)defl->z.avail_out);

    if (!fifo_buffer_empty(defl->buffer))
        deflate_try_write(defl);
}

static void
istream_deflate_force_read(struct istream_deflate *defl)
{
    int had_input = 0;

    defl->had_output = 0;

    while (1) {
        defl->had_input = 0;
        istream_read(defl->input);
        if (defl->input == NULL || defl->had_output)
            return;

        if (!defl->had_input)
            break;

        had_input = 1;
    }

    if (had_input)
        deflate_try_flush(defl);
}

static void
deflate_try_finish(struct istream_deflate *defl)
{
    void *dest_buffer;
    size_t max_length;
    int err;

    assert(!defl->z_stream_end);

    dest_buffer = deflate_buffer_write(defl, &max_length);
    if (dest_buffer == NULL)
        return;

    defl->z.next_out = dest_buffer;
    defl->z.avail_out = (uInt)max_length;

    defl->z.next_in = NULL;
    defl->z.avail_in = 0;

    err = deflate(&defl->z, Z_FINISH);
    if (err == Z_STREAM_END)
        defl->z_stream_end = 1;
    else if (err != Z_OK) {
        daemon_log(2, "deflate(Z_FINISH) failed: %d\n", err);
        deflate_close(defl);
        return;
    }

    fifo_buffer_append(defl->buffer, max_length - (size_t)defl->z.avail_out);

    if (defl->z_stream_end && fifo_buffer_empty(defl->buffer)) {
        deflate_close(defl);
        istream_invoke_eof(&defl->output);
    } else
        deflate_try_write(defl);
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

    defl->had_input = 1;

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
        defl->had_output = 1;
        fifo_buffer_append(defl->buffer, nbytes);

        pool_ref(defl->output.pool);
        deflate_try_write(defl);

        if (!defl->z_initialized) {
            pool_unref(defl->output.pool);
            return 0;
        }

        pool_unref(defl->output.pool);
    }

    return length - (size_t)defl->z.avail_in;
}

static void
deflate_input_eof(void *ctx)
{
    struct istream_deflate *defl = ctx;
    int err;

    assert(defl->input != NULL);
    istream_clear_unref(&defl->input);

    err = deflate_initialize_z(defl);
    if (err != Z_OK)
        return;

    pool_ref(defl->output.pool);
    deflate_try_finish(defl);
    pool_unref(defl->output.pool);
}

static void
deflate_input_abort(void *ctx)
{
    struct istream_deflate *defl = ctx;

    istream_clear_unref(&defl->input);

    deflate_close(defl);

    istream_invoke_abort(&defl->output);
}

static const struct istream_handler deflate_input_handler = {
    .data = deflate_input_data,
    .eof = deflate_input_eof,
    .abort = deflate_input_abort,
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

    if (!fifo_buffer_empty(defl->buffer))
        deflate_try_write(defl);
    else if (defl->input == NULL)
        deflate_try_finish(defl);
    else
        istream_deflate_force_read(defl);
}

static void
istream_deflate_close(istream_t istream)
{
    struct istream_deflate *defl = istream_to_deflate(istream);

    deflate_abort(defl);
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
    defl->z_stream_end = 0;

    istream_assign_ref_handler(&defl->input, input,
                               &deflate_input_handler, defl,
                               0);

    return istream_struct_cast(&defl->output);
}
