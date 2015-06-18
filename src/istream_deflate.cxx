/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_deflate.hxx"
#include "istream_internal.hxx"
#include "pool.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"
#include "util/StaticFifoBuffer.hxx"

#include <daemon/log.h>

#include <zlib.h>

#include <assert.h>

struct DeflateIstream {
    struct istream output;
    struct istream *input;
    bool z_initialized, z_stream_end;
    z_stream z;
    bool had_input, had_output;
    StaticFifoBuffer<uint8_t, 4096> buffer;

    DeflateIstream(struct pool &pool, struct istream &_input);

    bool InitZlib();

    void DeinitZlib() {
        if (z_initialized) {
            z_initialized = false;
            deflateEnd(&z);
        }
    }

    void Abort(GError *error) {
        DeinitZlib();

        if (input != nullptr)
            istream_free_handler(&input);

        istream_deinit_abort(&output, error);
    }

    /**
     * Submit data from the buffer to our istream handler.
     *
     * @return the number of bytes which were handled, or 0 if the
     * stream was closed
     */
    size_t TryWrite();

    /**
     * Starts to write to the buffer.
     *
     * @return a pointer to the writable buffer, or nullptr if there is no
     * room (our istream handler blocks) or if the stream was closed
     */
    WritableBuffer<void> BufferWrite() {
        auto w = buffer.Write();
        if (w.IsEmpty() && TryWrite() > 0)
            w = buffer.Write();

        return w.ToVoid();
    }

    void TryFlush();

    /**
     * Read from our input until we have submitted some bytes to our
     * istream handler.
     */
    void ForceRead();

    void TryFinish();
};

gcc_const
static GQuark
zlib_quark(void)
{
    return g_quark_from_static_string("zlib");
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

bool
DeflateIstream::InitZlib()
{
    if (z_initialized)
        return true;

    z.zalloc = z_alloc;
    z.zfree = z_free;
    z.opaque = output.pool;

    int err = deflateInit(&z, Z_DEFAULT_COMPRESSION);
    if (err != Z_OK) {
        GError *error =
            g_error_new(zlib_quark(), err,
                        "deflateInit(Z_FINISH) failed: %d", err);
        Abort(error);
        return false;
    }

    z_initialized = true;
    return true;
}

size_t
DeflateIstream::TryWrite()
{
    auto r = buffer.Read();
    assert(!r.IsEmpty());

    size_t nbytes = istream_invoke_data(&output, r.data, r.size);
    if (nbytes == 0)
        return 0;

    buffer.Consume(nbytes);

    if (nbytes == r.size && input == nullptr && z_stream_end) {
        DeinitZlib();
        istream_deinit_eof(&output);
        return 0;
    }

    return nbytes;
}

inline void
DeflateIstream::TryFlush()
{
    assert(!z_stream_end);

    auto w = BufferWrite();
    if (w.IsEmpty())
        return;

    z.next_out = (Bytef *)w.data;
    z.avail_out = (uInt)w.size;

    z.next_in = nullptr;
    z.avail_in = 0;

    int err = deflate(&z, Z_SYNC_FLUSH);
    if (err != Z_OK) {
        GError *error =
            g_error_new(zlib_quark(), err,
                        "deflate(Z_SYNC_FLUSH) failed: %d", err);
        Abort(error);
        return;
    }

    buffer.Append(w.size - (size_t)z.avail_out);

    if (!buffer.IsEmpty())
        TryWrite();
}

inline
void
DeflateIstream::ForceRead()
{
    bool had_input2 = false;
    had_output = false;

    pool_ref(output.pool);

    while (1) {
        had_input = false;
        istream_read(input);
        if (input == nullptr || had_output) {
            pool_unref(output.pool);
            return;
        }

        if (!had_input)
            break;

        had_input2 = true;
    }

    pool_unref(output.pool);

    if (had_input2)
        TryFlush();
}

void
DeflateIstream::TryFinish()
{
    assert(!z_stream_end);

    auto w = BufferWrite();
    if (w.IsEmpty())
        return;

    z.next_out = (Bytef *)w.data;
    z.avail_out = (uInt)w.size;

    z.next_in = nullptr;
    z.avail_in = 0;

    int err = deflate(&z, Z_FINISH);
    if (err == Z_STREAM_END)
        z_stream_end = true;
    else if (err != Z_OK) {
        GError *error =
            g_error_new(zlib_quark(), err,
                        "deflate(Z_FINISH) failed: %d", err);
        Abort(error);
        return;
    }

    buffer.Append(w.size - (size_t)z.avail_out);

    if (z_stream_end && buffer.IsEmpty()) {
        DeinitZlib();
        istream_deinit_eof(&output);
    } else
        TryWrite();
}


/*
 * istream handler
 *
 */

static size_t
deflate_input_data(const void *data, size_t length, void *ctx)
{
    DeflateIstream *defl = (DeflateIstream *)ctx;

    assert(defl->input != nullptr);

    auto w = defl->BufferWrite();
    if (w.size < 64) /* reserve space for end-of-stream marker */
        return 0;

    if (!defl->InitZlib())
        return 0;

    defl->had_input = true;

    defl->z.next_out = (Bytef *)w.data;
    defl->z.avail_out = (uInt)w.size;

    defl->z.next_in = (Bytef *)const_cast<void *>(data);
    defl->z.avail_in = (uInt)length;

    do {
        auto err = deflate(&defl->z, Z_NO_FLUSH);
        if (err != Z_OK) {
            GError *error =
                g_error_new(zlib_quark(), err,
                            "deflate() failed: %d", err);
            defl->Abort(error);
            return 0;
        }

        size_t nbytes = w.size - (size_t)defl->z.avail_out;
        if (nbytes > 0) {
            defl->had_output = true;
            defl->buffer.Append(nbytes);

            pool_ref(defl->output.pool);
            defl->TryWrite();

            if (!defl->z_initialized) {
                pool_unref(defl->output.pool);
                return 0;
            }

            pool_unref(defl->output.pool);
        } else
            break;

        w = defl->BufferWrite();
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
    DeflateIstream *defl = (DeflateIstream *)ctx;

    assert(defl->input != nullptr);
    defl->input = nullptr;

    if (!defl->InitZlib())
        return;

    defl->TryFinish();
}

static void
deflate_input_abort(GError *error, void *ctx)
{
    DeflateIstream *defl = (DeflateIstream *)ctx;

    assert(defl->input != nullptr);
    defl->input = nullptr;

    defl->DeinitZlib();

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

static inline DeflateIstream *
istream_to_deflate(struct istream *istream)
{
    return &ContainerCast2(*istream, &DeflateIstream::output);
}

static void
istream_deflate_read(struct istream *istream)
{
    DeflateIstream *defl = istream_to_deflate(istream);

    if (!defl->buffer.IsEmpty())
        defl->TryWrite();
    else if (defl->input == nullptr)
        defl->TryFinish();
    else
        defl->ForceRead();
}

static void
istream_deflate_close(struct istream *istream)
{
    DeflateIstream *defl = istream_to_deflate(istream);

    defl->DeinitZlib();

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

inline
DeflateIstream::DeflateIstream(struct pool &pool, struct istream &_input)
    :z_initialized(false), z_stream_end(false)
{
    istream_init(&output, &istream_deflate, &pool);

    buffer.Clear();

    istream_assign_handler(&input, &_input,
                           &deflate_input_handler, this,
                           0);
}

struct istream *
istream_deflate_new(struct pool *pool, struct istream *input)
{
    assert(input != nullptr);
    assert(!istream_has_handler(input));

    auto *defl = NewFromPool<DeflateIstream>(*pool, *pool, *input);
    return &defl->output;
}
