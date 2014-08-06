/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_deflate.hxx"
#include "istream-internal.h"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"
#include "util/StaticFifoBuffer.hxx"

#include <daemon/log.h>

#include <zlib.h>

#include <assert.h>

struct istream_deflate {
    struct istream output;
    struct istream *input;
    bool z_initialized, z_stream_end;
    z_stream z;
    bool had_input, had_output;
    StaticFifoBuffer<uint8_t, 4096> buffer;
};

gcc_const
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

    if (defl->input != nullptr)
        istream_free_handler(&defl->input);

    istream_deinit_abort(&defl->output, error);
}

static voidpf z_alloc
(voidpf opaque, uInt items, uInt size)
{
    struct pool *pool = (struct pool *)opaque;

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
    if (defl->z_initialized)
        return Z_OK;

    defl->z.zalloc = z_alloc;
    defl->z.zfree = z_free;
    defl->z.opaque = defl->output.pool;

    int err = deflateInit(&defl->z, Z_DEFAULT_COMPRESSION);
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
    auto r = defl->buffer.Read();
    assert(!r.IsEmpty());

    size_t nbytes = istream_invoke_data(&defl->output, r.data, r.size);
    if (nbytes == 0)
        return 0;

    defl->buffer.Consume(nbytes);

    if (nbytes == r.size && defl->input == nullptr && defl->z_stream_end) {
        deflate_close(defl);
        istream_deinit_eof(&defl->output);
        return 0;
    }

    return nbytes;
}

/**
 * Starts to write to the buffer.
 *
 * @return a pointer to the writable buffer, or nullptr if there is no
 * room (our istream handler blocks) or if the stream was closed
 */
static WritableBuffer<void>
deflate_buffer_write(struct istream_deflate *defl)
{
    auto w = defl->buffer.Write();
    if (w.IsEmpty() && deflate_try_write(defl) > 0)
        w = defl->buffer.Write();

    return w.ToVoid();
}

static void
deflate_try_flush(struct istream_deflate *defl)
{
    assert(!defl->z_stream_end);

    auto w = deflate_buffer_write(defl);
    if (w.IsEmpty())
        return;

    defl->z.next_out = (Bytef *)w.data;
    defl->z.avail_out = (uInt)w.size;

    defl->z.next_in = nullptr;
    defl->z.avail_in = 0;

    int err = deflate(&defl->z, Z_SYNC_FLUSH);
    if (err != Z_OK) {
        GError *error =
            g_error_new(zlib_quark(), err,
                        "deflate(Z_SYNC_FLUSH) failed: %d", err);
        deflate_abort(defl, error);
        return;
    }

    defl->buffer.Append(w.size - (size_t)defl->z.avail_out);

    if (!defl->buffer.IsEmpty())
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
        if (defl->input == nullptr || defl->had_output) {
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
    assert(!defl->z_stream_end);

    auto w = deflate_buffer_write(defl);
    if (w.IsEmpty())
        return;

    defl->z.next_out = (Bytef *)w.data;
    defl->z.avail_out = (uInt)w.size;

    defl->z.next_in = nullptr;
    defl->z.avail_in = 0;

    int err = deflate(&defl->z, Z_FINISH);
    if (err == Z_STREAM_END)
        defl->z_stream_end = true;
    else if (err != Z_OK) {
        GError *error =
            g_error_new(zlib_quark(), err,
                        "deflate(Z_FINISH) failed: %d", err);
        deflate_abort(defl, error);
        return;
    }

    defl->buffer.Append(w.size - (size_t)defl->z.avail_out);

    if (defl->z_stream_end && defl->buffer.IsEmpty()) {
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
    struct istream_deflate *defl = (struct istream_deflate *)ctx;

    assert(defl->input != nullptr);

    auto w = deflate_buffer_write(defl);
    if (w.size < 64) /* reserve space for end-of-stream marker */
        return 0;

    int err = deflate_initialize_z(defl);
    if (err != Z_OK)
        return 0;

    defl->had_input = true;

    defl->z.next_out = (Bytef *)w.data;
    defl->z.avail_out = (uInt)w.size;

    defl->z.next_in = (Bytef *)const_cast<void *>(data);
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

        size_t nbytes = w.size - (size_t)defl->z.avail_out;
        if (nbytes > 0) {
            defl->had_output = true;
            defl->buffer.Append(nbytes);

            pool_ref(defl->output.pool);
            deflate_try_write(defl);

            if (!defl->z_initialized) {
                pool_unref(defl->output.pool);
                return 0;
            }

            pool_unref(defl->output.pool);
        } else
            break;

        w = deflate_buffer_write(defl);
        if (w.size < 64) /* reserve space for end-of-stream marker */
            break;

        defl->z.next_out = (Bytef *)w.data;
        defl->z.avail_out = (uInt)w.size;
    } while (defl->z.avail_in > 0);

    return length - (size_t)defl->z.avail_in;
}

static void
deflate_input_eof(void *ctx)
{
    struct istream_deflate *defl = (struct istream_deflate *)ctx;

    assert(defl->input != nullptr);
    defl->input = nullptr;

    int err = deflate_initialize_z(defl);
    if (err != Z_OK)
        return;

    deflate_try_finish(defl);
}

static void
deflate_input_abort(GError *error, void *ctx)
{
    struct istream_deflate *defl = (struct istream_deflate *)ctx;

    assert(defl->input != nullptr);
    defl->input = nullptr;

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
    return &ContainerCast2(*istream, &istream_deflate::output);
}

static void
istream_deflate_read(struct istream *istream)
{
    struct istream_deflate *defl = istream_to_deflate(istream);

    if (!defl->buffer.IsEmpty())
        deflate_try_write(defl);
    else if (defl->input == nullptr)
        deflate_try_finish(defl);
    else
        istream_deflate_force_read(defl);
}

static void
istream_deflate_close(struct istream *istream)
{
    struct istream_deflate *defl = istream_to_deflate(istream);

    deflate_close(defl);

    if (defl->input != nullptr)
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

    assert(input != nullptr);
    assert(!istream_has_handler(input));

    defl->buffer.Clear();
    defl->z_initialized = false;
    defl->z_stream_end = false;

    istream_assign_handler(&defl->input, input,
                           &deflate_input_handler, defl,
                           0);

    return istream_struct_cast(&defl->output);
}
