/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_STICKY_HASH_HXX
#define BENG_PROXY_STICKY_HASH_HXX

#include <stdint.h>

/**
 * A type which can store a hash for choosing a cluster member.  Zero
 * is a special value for "sticky disabled"
 */
typedef uint32_t sticky_hash_t;

#endif
