/*
 * Format stuff into string.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FORMAT_H
#define __BENG_FORMAT_H

#include <inline/compiler.h>

#include <stdint.h>
#include <string.h>

static gcc_always_inline void
format_2digit(char *dest, unsigned number)
{
    dest[0] = (char)('0' + (number / 10));
    dest[1] = (char)('0' + number % 10);
}

static gcc_always_inline void
format_4digit(char *dest, unsigned number)
{
    dest[0] = (char)('0' + number / 1000);
    dest[1] = (char)('0' + (number / 100) % 10);
    dest[2] = (char)('0' + (number / 10) % 10);
    dest[3] = (char)('0' + number % 10);
}

/**
 * Format a 64 bit unsigned integer into a decimal string.
 */
static gcc_always_inline size_t
format_uint64(char dest[32], uint64_t number)
{
    char *p = dest + 32 - 1;

    *p = 0;
    do {
        --p;
        *p = '0' + (char)(number % 10);
        number /= 10;
    } while (number != 0);

    if (p > dest)
        memmove(dest, p, dest + 32 - p);

    return dest + 32 - p - 1;
}

#endif
