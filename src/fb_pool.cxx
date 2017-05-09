/*
 * An allocator for fifo_buffer objects that can return unused memory
 * back to the kernel.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fb_pool.hxx"
#include "SlicePool.hxx"

#include <assert.h>

/* TODO: this increase from 8 kB to 16 kB is only here to work around
 * a OpenSSL problem; TLS packets may be up to 16 kB, and our
 * FifoBufferBio can't handle the resulting SSL_ERROR_WANT_READ when
 * the buffer is already full; we need a better general solution for
 * this */
/* TODO: after restoring this buffer to 8 kB, shrink the stack buffer
 * in ThreadSocketFilter::SubmitDecryptedInput() as well */
static constexpr size_t FB_SIZE = 2 * 8192;
//static constexpr size_t FB_SIZE = 8192;

class SliceFifoBufferPool {
    SlicePool *const pool;

public:
    SliceFifoBufferPool()
        :pool(slice_pool_new(FB_SIZE, 256)) {
        assert(pool != nullptr);
    }

    ~SliceFifoBufferPool() {
        slice_pool_free(pool);
    }

    SlicePool &Get() {
        return *pool;
    }

    void ForkCow(bool inherit) {
        slice_pool_fork_cow(*pool, inherit);
    }

    void Compress() {
        slice_pool_compress(pool);
    }
};

static SliceFifoBufferPool *fb_pool;

void
fb_pool_init()
{
    assert(fb_pool == nullptr);

    fb_pool = new SliceFifoBufferPool();
}

void
fb_pool_deinit(void)
{
    assert(fb_pool != nullptr);

    delete fb_pool;
    fb_pool = nullptr;
}

void
fb_pool_fork_cow(bool inherit)
{
    assert(fb_pool != nullptr);

    fb_pool->ForkCow(inherit);
}

SlicePool &
fb_pool_get()
{
    return fb_pool->Get();
}

void
fb_pool_compress(void)
{
    assert(fb_pool != nullptr);

    fb_pool->Compress();
}
