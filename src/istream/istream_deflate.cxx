/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "istream_deflate.hxx"
#include "FacadeIstream.hxx"
#include "pool.hxx"
#include "fb_pool.hxx"
#include "SliceFifoBuffer.hxx"
#include "event/DeferEvent.hxx"
#include "event/Callback.hxx"
#include "util/Cast.hxx"
#include "util/ConstBuffer.hxx"
#include "util/WritableBuffer.hxx"

#include <daemon/log.h>

#include <zlib.h>

#include <glib.h>

#include <assert.h>

class DeflateIstream final : public FacadeIstream {
    const bool gzip;
    bool z_initialized = false, z_stream_end = false;
    z_stream z;
    bool had_input, had_output;
    bool reading;
    SliceFifoBuffer buffer;

    /**
     * This callback is used to request more data from the input if an
     * OnData() call did not produce any output.  This tries to
     * prevent stalling the stream.
     */
    DeferEvent defer;

public:
    DeflateIstream(struct pool &_pool, Istream &_input, bool _gzip)
        :FacadeIstream(_pool, _input,
                       MakeIstreamHandler<DeflateIstream>::handler, this),
         gzip(_gzip),
         reading(false),
         defer(MakeSimpleEventCallback(DeflateIstream, OnDeferred), this)
    {
    }

    ~DeflateIstream() {
        defer.Deinit();
        buffer.FreeIfDefined(fb_pool_get());
    }

    bool InitZlib();

    void DeinitZlib() {
        if (z_initialized) {
            z_initialized = false;
            deflateEnd(&z);
        }
    }

    void Abort(GError *error) {
        DeinitZlib();

        if (HasInput())
            ClearAndCloseInput();

        DestroyError(error);
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
        buffer.AllocateIfNull(fb_pool_get());
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

    /* virtual methods from class Istream */

    void _Read() override {
        if (!buffer.IsEmpty())
            TryWrite();
        else if (HasInput())
            ForceRead();
        else
            TryFinish();
    }

    void _Close() override {
        DeinitZlib();

        if (HasInput())
            input.Close();

        Destroy();
    }

    /* handler */
    size_t OnData(const void *data, size_t length);

    ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
                     gcc_unused size_t max_length) {
        gcc_unreachable();
    }

    void OnEof();
    void OnError(GError *error);

private:
    void OnDeferred() {
        assert(HasInput());

        ForceRead();
    }

    int GetWindowBits() const {
        return MAX_WBITS + gzip * 16;
    }
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
    z.opaque = &GetPool();

    int err = deflateInit2(&z, Z_DEFAULT_COMPRESSION,
                           Z_DEFLATED, GetWindowBits(), 8,
                           Z_DEFAULT_STRATEGY);
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

    size_t nbytes = InvokeData(r.data, r.size);
    if (nbytes == 0)
        return 0;

    buffer.Consume(nbytes);
    buffer.FreeIfEmpty(fb_pool_get());

    if (nbytes == r.size && !HasInput() && z_stream_end) {
        DeinitZlib();
        DestroyEof();
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
    assert(!reading);

    const ScopePoolRef ref(GetPool() TRACE_ARGS);

    bool had_input2 = false;
    had_output = false;

    while (1) {
        had_input = false;
        reading = true;
        input.Read();
        reading = false;
        if (!HasInput() || had_output)
            return;

        if (!had_input)
            break;

        had_input2 = true;
    }

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
        DestroyEof();
    } else
        TryWrite();
}


/*
 * istream handler
 *
 */

size_t
DeflateIstream::OnData(const void *data, size_t length)
{
    assert(HasInput());

    auto w = BufferWrite();
    if (w.size < 64) /* reserve space for end-of-stream marker */
        return 0;

    if (!InitZlib())
        return 0;

    had_input = true;

    if (!reading)
        had_output = false;

    z.next_out = (Bytef *)w.data;
    z.avail_out = (uInt)w.size;

    z.next_in = (Bytef *)const_cast<void *>(data);
    z.avail_in = (uInt)length;

    do {
        auto err = deflate(&z, Z_NO_FLUSH);
        if (err != Z_OK) {
            GError *error =
                g_error_new(zlib_quark(), err,
                            "deflate() failed: %d", err);
            Abort(error);
            return 0;
        }

        size_t nbytes = w.size - (size_t)z.avail_out;
        if (nbytes > 0) {
            had_output = true;
            buffer.Append(nbytes);

            const ScopePoolRef ref(GetPool() TRACE_ARGS);
            TryWrite();

            if (!z_initialized)
                return 0;
        } else
            break;

        w = BufferWrite();
        if (w.size < 64) /* reserve space for end-of-stream marker */
            break;

        z.next_out = (Bytef *)w.data;
        z.avail_out = (uInt)w.size;
    } while (z.avail_in > 0);

    if (!reading && !had_output)
        /* we received data from our input, but we did not produce any
           output (and we're not looping inside ForceRead()) - to
           avoid stalling the stream, trigger the DeferEvent */
        defer.Add();

    return length - (size_t)z.avail_in;
}

void
DeflateIstream::OnEof()
{
    ClearInput();
    defer.Cancel();

    if (!InitZlib())
        return;

    TryFinish();
}

void
DeflateIstream::OnError(GError *error)
{
    ClearInput();

    DeinitZlib();

    DestroyError(error);
}

/*
 * constructor
 *
 */

Istream *
istream_deflate_new(struct pool *pool, Istream &input, bool gzip)
{
    return NewIstream<DeflateIstream>(*pool, input, gzip);
}
