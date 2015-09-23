/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CLOCK_H
#define BENG_PROXY_CLOCK_H

#include <inline/compiler.h>

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Returns the current monotonic time stamp in seconds.
 */
gcc_pure
unsigned
now_s(void);

/**
 * Returns the current monotonic time stamp in microseconds.
 */
gcc_pure
uint64_t
now_us(void);

#ifdef __cplusplus
}
#endif

#endif
