/*
 * An allocator for fifo_buffer objects that can return unused memory
 * back to the kernel.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FB_POOL_HXX
#define BENG_PROXY_FB_POOL_HXX

#include <inline/compiler.h>

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

gcc_const
struct slice_pool &
fb_pool_get();

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

#endif
