/*
 * Format stuff into string.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FORMAT_H
#define __BENG_FORMAT_H

#include "compiler.h"

#include <stdint.h>
#include <string.h>

static attr_always_inline void
format_2digit(char *dest, unsigned number)
{
    dest[0] = '0' + number / 10;
    dest[1] = '0' + number % 10;
}

static attr_always_inline void
format_4digit(char *dest, unsigned number)
{
    dest[0] = '0' + number / 1000;
    dest[1] = '0' + (number / 100) % 10;
    dest[2] = '0' + (number / 10) % 10;
    dest[3] = '0' + number % 10;
}

/**
 * Format a 64 bit unsigned integer into a decimal string.
 */
static attr_always_inline void
format_uint64(char dest[32], uint64_t number)
{
    char *p = dest + sizeof(dest) - 1;

    *p = 0;
    while (number != 0) {
        --p;
        *p = '0' + (number % 10);
        number /= 10;
    }

    if (p > dest)
        memmove(dest, p, dest + sizeof(dest) - p);
}

#endif
