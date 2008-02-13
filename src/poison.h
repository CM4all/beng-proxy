/*
 * Memory poisoning.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_POISON_H
#define __BENG_POISON_H

#include "valgrind.h"

#ifdef POISON
#include <string.h>
#endif

static inline void
poison_noaccess(void *p, size_t length)
{
#ifndef POISON
    memset(p, 0x01, length);
#endif
    VALGRIND_MAKE_MEM_NOACCESS(p, length);
}

static inline void
poison_undefined(void *p, size_t length)
{
#ifndef POISON
    memset(p, 0x02, length);
#endif
    VALGRIND_MAKE_MEM_UNDEFINED(p, length);
}

#endif
