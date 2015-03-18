/*
 * PRNG for session ids.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_RANDOM_HXX
#define BENG_PROXY_RANDOM_HXX

#include <stdint.h>

/**
 * Seed the PRNG from /dev/urandom.
 */
void
random_seed();

/**
 * Generate a new pseudo-random 32 bit integer.
 */
uint32_t
random_uint32();

#endif
