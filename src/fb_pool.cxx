/*
 * An allocator for fifo_buffer objects that can return unused memory
 * back to the kernel.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "fb_pool.h"
#include "slice.hxx"
#include "fifo-buffer.h"
#include "cleanup_timer.h"

#include <assert.h>

static const size_t FB_SIZE = 8192;

struct fbp_meta {
    struct slice_area *area;
};

static struct slice_pool *fb_pool;
static bool fb_auto_cleanup;
static struct cleanup_timer fb_cleanup_timer;

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

    cleanup_timer_init(&fb_cleanup_timer, 600, fb_pool_cleanup, nullptr);
}

void
fb_pool_deinit(void)
{
    assert(fb_pool != nullptr);

    cleanup_timer_disable(&fb_cleanup_timer);
    slice_pool_free(fb_pool);
}

void
fb_pool_disable(void)
{
    assert(fb_pool != nullptr);

    cleanup_timer_disable(&fb_cleanup_timer);
}

void
fb_pool_compress(void)
{
    assert(fb_pool != nullptr);

    slice_pool_compress(fb_pool);
    cleanup_timer_disable(&fb_cleanup_timer);
}

static inline struct fbp_meta *
to_meta(struct fifo_buffer *buffer)
{
    char *p = (char *)buffer;
    p += FB_SIZE - sizeof(struct fbp_meta);
    void *q = p;
    return (struct fbp_meta *)q;
}

struct fifo_buffer *
fb_pool_alloc(void)
{
    struct slice_area *area = slice_pool_get_area(fb_pool);
    assert(area != nullptr);

    struct fifo_buffer *buffer = (struct fifo_buffer *)
        slice_alloc(fb_pool, area);
    assert(buffer != nullptr);

    struct fbp_meta *meta = to_meta(buffer);
    meta->area = area;

    fifo_buffer_init(buffer, FB_SIZE - sizeof(*meta));
    return buffer;
}

void
fb_pool_free(struct fifo_buffer *buffer)
{
    assert(buffer != nullptr);

    struct fbp_meta *meta = to_meta(buffer);
    slice_free(fb_pool, meta->area, buffer);

    /* schedule cleanup every 10 minutes */
    if (fb_auto_cleanup)
        cleanup_timer_enable(&fb_cleanup_timer);
}
