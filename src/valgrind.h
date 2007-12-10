/*
 * Valgrind helper functions.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_VALGRIND_H
#define __BENG_VALGRIND_H

#ifdef VALGRIND

#include <valgrind/memcheck.h>

#else /* VALGRIND */

#include "compiler.h"

static inline void
VALGRIND_MAKE_MEM_NOACCESS(void *buffer attr_unused, size_t length attr_unused)
{
}

static inline void
VALGRIND_MAKE_MEM_UNDEFINED(void *buffer attr_unused, size_t length attr_unused)
{
}

#endif /* VALGRIND */

#endif
