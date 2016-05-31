/*
 * An allocator for fifo_buffer objects that can return unused memory
 * back to the kernel.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fb_pool.hxx"
#include "SlicePool.hxx"
#include "event/CleanupTimer.hxx"

#include <assert.h>

static constexpr size_t FB_SIZE = 8192;

class SliceFifoBufferPool {
    SlicePool *const pool;
    CleanupTimer cleanup_timer;

public:
    SliceFifoBufferPool(EventLoop &event_loop, bool auto_cleanup)
        :pool(slice_pool_new(FB_SIZE, 256)),
         cleanup_timer(event_loop, 600, BIND_THIS_METHOD(CleanupCallback)) {
        assert(pool != nullptr);

        if (auto_cleanup)
            cleanup_timer.Enable();
    }

    ~SliceFifoBufferPool() {
        slice_pool_free(pool);
    }

    SlicePool &Get() {
        return *pool;
    }

    void Disable() {
        cleanup_timer.Disable();
    }

    void ForkCow(bool inherit) {
        slice_pool_fork_cow(*pool, inherit);
    }

    void Compress() {
        slice_pool_compress(pool);
    }

private:
    bool CleanupCallback() {
        fb_pool_compress();
        return true;
    }
};

static SliceFifoBufferPool *fb_pool;

void
fb_pool_init(EventLoop &event_loop, bool auto_cleanup)
{
    assert(fb_pool == nullptr);

    fb_pool = new SliceFifoBufferPool(event_loop, auto_cleanup);
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
fb_pool_disable(void)
{
    assert(fb_pool != nullptr);

    fb_pool->Disable();
}

void
fb_pool_compress(void)
{
    assert(fb_pool != nullptr);

    fb_pool->Compress();
}
