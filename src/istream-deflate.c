/*
 * This istream filter removes HTTP chunking.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream-internal.h"
#include "fifo-buffer.h"

#include <daemon/log.h>

#include <assert.h>
#include <zlib.h>

struct istream_deflate {
    struct istream output;
    struct istream *input;
    struct fifo_buffer *buffer;
    bool z_initialized, z_stream_end;
    z_stream z;
    bool had_input, had_output;
};

static GQuark
zlib_quark(void)
{
    return g_quark_from_static_string("zlib");
}

static void
deflate_close(struct istream_deflate *defl)
{
    if (defl->z_initialized) {
        defl->z_initialized = false;
        deflateEnd(&defl->z);
    }
}

static void
deflate_abort(struct istream_deflate *defl, GError *error)
{
    deflate_close(defl);

    if (defl->input != NULL)
        istream_free_handler(&defl->input);

    istream_deinit_abort(&defl->output, error);
}

static voidpf z_alloc
(voidpf opaque, uInt items, uInt size)
{
    struct pool *pool = opaque;

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
        GError *error =
            g_error_new(zlib_quark(), err,
                        "deflateInit(Z_FINISH) failed: %d", err);
        deflate_abort(defl, error);
        return err;
    }

    defl->z_initialized = true;
    return Z_OK;
}

/**
 * Submit data from the buffer to our istream handler.
 *
 * @return the number of bytes which were handled, or 0 if the stream
 * was closed
 */
static size_t
deflate_try_write(struct istream_deflate *defl)
{
    const void *data;
    size_t length, nbytes;

    data = fifo_buffer_read(defl->buffer, &length);
    assert(data != NULL);

    nbytes = istream_invoke_data(&defl->output, data, length);
    if (nbytes == 0)
        return 0;

    fifo_buffer_consume(defl->buffer, nbytes);

    if (nbytes == length && defl->input == NULL && defl->z_stream_end) {
        deflate_close(defl);
        istream_deinit_eof(&defl->output);
        return 0;
    }

    return nbytes;
}

/**
 * Starts to write to the buffer.
 *
 * @return a pointer to the writable buffer, or NULL if there is no
 * room (our istream handler blocks) or if the stream was closed
 */
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
        GError *error =
            g_error_new(zlib_quark(), err,
                        "deflate(Z_SYNC_FLUSH) failed: %d", err);
        deflate_abort(defl, error);
        return;
    }

    fifo_buffer_append(defl->buffer, max_length - (size_t)defl->z.avail_out);

    if (!fifo_buffer_empty(defl->buffer))
        deflate_try_write(defl);
}

/**
 * Read from our input until we have submitted some bytes to our
 * istream handler.
 */
static void
istream_deflate_force_read(struct istream_deflate *defl)
{
    bool had_input = false;

    defl->had_output = false;

    pool_ref(defl->output.pool);

    while (1) {
        defl->had_input = false;
        istream_read(defl->input);
        if (defl->input == NULL || defl->had_output) {
            pool_unref(defl->output.pool);
            return;
        }

        if (!defl->had_input)
            break;

        had_input = true;
    }

    pool_unref(defl->output.pool);

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
        defl->z_stream_end = true;
    else if (err != Z_OK) {
        GError *error =
            g_error_new(zlib_quark(), err,
                        "deflate(Z_FINISH) failed: %d", err);
        deflate_abort(defl, error);
        return;
    }

    fifo_buffer_append(defl->buffer, max_length - (size_t)defl->z.avail_out);

    if (defl->z_stream_end && fifo_buffer_empty(defl->buffer)) {
        deflate_close(defl);
        istream_deinit_eof(&defl->output);
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

    defl->had_input = true;

    defl->z.next_out = dest_buffer;
    defl->z.avail_out = (uInt)max_length;

    deconst.input = data;
    defl->z.next_in = deconst.output;
    defl->z.avail_in = (uInt)length;

    do {
        err = deflate(&defl->z, Z_NO_FLUSH);
        if (err != Z_OK) {
            GError *error =
                g_error_new(zlib_quark(), err,
                            "deflate() failed: %d", err);
            deflate_abort(defl, error);
            return 0;
        }

        nbytes = max_length - (size_t)defl->z.avail_out;
        if (nbytes > 0) {
            defl->had_output = true;
            fifo_buffer_append(defl->buffer, nbytes);

            pool_ref(defl->output.pool);
            deflate_try_write(defl);

            if (!defl->z_initialized) {
                pool_unref(defl->output.pool);
                return 0;
            }

            pool_unref(defl->output.pool);
        } else
            break;

        dest_buffer = deflate_buffer_write(defl, &max_length);
        if (dest_buffer == NULL || max_length < 64) /* reserve space for end-of-stream marker */
            break;

        defl->z.next_out = dest_buffer;
        defl->z.avail_out = (uInt)max_length;
    } while (defl->z.avail_in > 0);

    return length - (size_t)defl->z.avail_in;
}

static void
deflate_input_eof(void *ctx)
{
    struct istream_deflate *defl = ctx;
    int err;

    assert(defl->input != NULL);
    defl->input = NULL;

    err = deflate_initialize_z(defl);
    if (err != Z_OK)
        return;

    deflate_try_finish(defl);
}

static void
deflate_input_abort(GError *error, void *ctx)
{
    struct istream_deflate *defl = ctx;

    assert(defl->input != NULL);
    defl->input = NULL;

    deflate_close(defl);

    istream_deinit_abort(&defl->output, error);
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
istream_to_deflate(struct istream *istream)
{
    return (struct istream_deflate *)(((char*)istream) - offsetof(struct istream_deflate, output));
}

static void
istream_deflate_read(struct istream *istream)
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
istream_deflate_close(struct istream *istream)
{
    struct istream_deflate *defl = istream_to_deflate(istream);

    deflate_close(defl);

    if (defl->input != NULL)
        istream_close_handler(defl->input);

    istream_deinit(&defl->output);
}

static const struct istream_class istream_deflate = {
    .read = istream_deflate_read,
    .close = istream_deflate_close,
};


/*
 * constructor
 *
 */

struct istream *
istream_deflate_new(struct pool *pool, struct istream *input)
{
    struct istream_deflate *defl = istream_new_macro(pool, deflate);

    assert(input != NULL);
    assert(!istream_has_handler(input));

    defl->buffer = fifo_buffer_new(pool, 4096);
    defl->z_initialized = false;
    defl->z_stream_end = false;

    istream_assign_handler(&defl->input, input,
                           &deflate_input_handler, defl,
                           0);

    return istream_struct_cast(&defl->output);
}
