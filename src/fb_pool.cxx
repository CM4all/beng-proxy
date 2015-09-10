/*
 * An allocator for fifo_buffer objects that can return unused memory
 * back to the kernel.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fb_pool.hxx"
#include "SlicePool.hxx"
#include "event/cleanup_timer.hxx"

#include <assert.h>

static constexpr size_t FB_SIZE = 8192;

static SlicePool *fb_pool;
static bool fb_auto_cleanup;
static CleanupTimer fb_cleanup_timer;

static bool
fb_pool_cleanup(gcc_unused void *ctx)
{
    fb_pool_compress();
    return false;
}

void
fb_pool_init(bool auto_cleanup)
{
    assert(fb_pool == nullptr);

    fb_auto_cleanup = auto_cleanup;

    fb_pool = slice_pool_new(FB_SIZE, 1024);
    assert(fb_pool != nullptr);

    fb_cleanup_timer.Init(600, fb_pool_cleanup, nullptr);
}

void
fb_pool_deinit(void)
{
    assert(fb_pool != nullptr);

    fb_cleanup_timer.Deinit();
    slice_pool_free(fb_pool);
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

    fb_cleanup_timer.Disable();
}

void
fb_pool_compress(void)
{
    assert(fb_pool != nullptr);

    slice_pool_compress(fb_pool);
    fb_cleanup_timer.Disable();
}
