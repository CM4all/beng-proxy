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

static SlicePool *fb_pool;
static CleanupTimer fb_cleanup_timer;

static bool
fb_pool_cleanup(gcc_unused void *ctx)
{
    fb_pool_compress();
    return true;
}

void
fb_pool_init(EventLoop &event_loop, bool auto_cleanup)
{
    assert(fb_pool == nullptr);

    fb_pool = slice_pool_new(FB_SIZE, 256);
    assert(fb_pool != nullptr);

    if (auto_cleanup) {
        fb_cleanup_timer.Init(event_loop, 600, fb_pool_cleanup, nullptr);
        fb_cleanup_timer.Enable();
    }
}

void
fb_pool_deinit(void)
{
    assert(fb_pool != nullptr);

    if (fb_cleanup_timer.IsInitialized())
        fb_cleanup_timer.Deinit();

    slice_pool_free(fb_pool);
    fb_pool = nullptr;
}

void
fb_pool_fork_cow(bool inherit)
{
    assert(fb_pool != nullptr);

    slice_pool_fork_cow(*fb_pool, inherit);
}

SlicePool &
fb_pool_get()
{
    return *fb_pool;
}

void
fb_pool_disable(void)
{
    assert(fb_pool != nullptr);

    if (fb_cleanup_timer.IsInitialized())
        fb_cleanup_timer.Disable();
}

void
fb_pool_compress(void)
{
    assert(fb_pool != nullptr);

    slice_pool_compress(fb_pool);
}
