/*
 * An allocator for fifo_buffer objects that can return unused memory
 * back to the kernel.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FB_POOL_HXX
#define BENG_PROXY_FB_POOL_HXX

#include "util/Compiler.h"

struct SlicePool;

/**
 * Global initialization.
 */
void
fb_pool_init();

/**
 * Global deinitialization.
 */
void
fb_pool_deinit();

void
fb_pool_fork_cow(bool inherit);

gcc_const
SlicePool &
fb_pool_get();

/**
 * Give free memory back to the kernel.  The library will
 * automatically do this once in a while.  This call forces immediate
 * cleanup.
 */
void
fb_pool_compress(void);

class ScopeFbPoolInit {
public:
    ScopeFbPoolInit() {
        fb_pool_init();
    }

    ~ScopeFbPoolInit() {
        fb_pool_deinit();
    }
};

#endif
