/*
 * An allocator for fifo_buffer objects that can return unused memory
 * back to the kernel.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FB_POOL_H
#define BENG_PROXY_FB_POOL_H

#include <inline/compiler.h>

#include <stdbool.h>

/**
 * Global initialization.
 */
void
fb_pool_init(bool auto_cleanup);

/**
 * Global deinitialization.
 */
void
fb_pool_deinit(void);

/**
 * Disable the cleanup timer.  Do this during shutdown, to let
 * libevent quit the main loop.
 */
void
fb_pool_disable(void);

/**
 * Give free memory back to the kernel.  The library will
 * automatically do this once in a while.  This call forces immediate
 * cleanup.
 */
void
fb_pool_compress(void);

gcc_malloc
struct fifo_buffer *
fb_pool_alloc(void);

gcc_nonnull_all
void
fb_pool_free(struct fifo_buffer *buffer);

#endif
