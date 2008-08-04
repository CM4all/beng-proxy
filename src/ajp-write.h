/*
 * Write an AJP stream.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_AJP_WRITE_H
#define __BENG_AJP_WRITE_H

#include "growing-buffer.h"

#include <netinet/in.h>

static void
gb_write_ajp_string(struct growing_buffer *gb, const char *s)
{
    size_t length = strlen(s);
    char *p;

    if (length > 0xffff)
        length = 0xffff; /* XXX too long, cut off */

    p = growing_buffer_write(gb, 2 + length + 1);
    *(uint16_t*)p = htons(length);
    memcpy(p + 2, s, length + 1);
}

static void
gb_write_ajp_integer(struct growing_buffer *gb, int i)
{
    uint16_t *p;

    p = growing_buffer_write(gb, sizeof(*p));
    *p = htons(i);
}

static void
gb_write_ajp_bool(struct growing_buffer *gb, bool b)
{
    bool *p;

    p = growing_buffer_write(gb, sizeof(*p));
    *p = b ? 1 : 0;
}

#endif
