/*
 * Format stuff into string.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_FORMAT_H
#define __BENG_FORMAT_H

#include "compiler.h"

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

#endif
