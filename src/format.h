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

extern const char hex_digits[0x10];


static attr_always_inline void
format_2digit(char *dest, unsigned number)
{
    dest[0] = (char)('0' + (number / 10));
    dest[1] = (char)('0' + number % 10);
}

static attr_always_inline void
format_4digit(char *dest, unsigned number)
{
    dest[0] = (char)('0' + number / 1000);
    dest[1] = (char)('0' + (number / 100) % 10);
    dest[2] = (char)('0' + (number / 10) % 10);
    dest[3] = (char)('0' + number % 10);
}

static attr_always_inline void
format_uint8_hex_fixed(char dest[2], uint8_t number) {
    dest[0] = hex_digits[(number >> 4) & 0xf];
    dest[1] = hex_digits[number & 0xf];
}

static attr_always_inline void
format_uint16_hex_fixed(char dest[4], uint16_t number) {
    dest[0] = hex_digits[(number >> 12) & 0xf];
    dest[1] = hex_digits[(number >> 8) & 0xf];
    dest[2] = hex_digits[(number >> 4) & 0xf];
    dest[3] = hex_digits[number & 0xf];
}

/**
 * Format a 64 bit unsigned integer into a decimal string.
 */
static attr_always_inline size_t
format_uint64(char dest[32], uint64_t number)
{
    char *p = dest + 32 - 1;

    *p = 0;
    do {
        --p;
        *p = '0' + (number % 10);
        number /= 10;
    } while (number != 0);

    if (p > dest)
        memmove(dest, p, dest + 32 - p);

    return dest + 32 - p - 1;
}

/**
 * Format a 32 bit unsigned integer into a hex string.
 */
static attr_always_inline size_t
format_uint32_hex(char dest[9], uint32_t number)
{
    char *p = dest + 9 - 1;

    *p = 0;
    do {
        --p;
        *p = hex_digits[number % 0x10];
        number /= 0x10;
    } while (number != 0);

    if (p > dest)
        memmove(dest, p, dest + 9 - p);

    return dest + 9 - p - 1;
}

#endif
