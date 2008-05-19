/*
 * Distributed memory pool in shared memory.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_DCHUNK_H
#define __BENG_DCHUNK_H

#include <inline/list.h>

#include <stddef.h>

struct dpool_allocation {
    struct list_head all_siblings, free_siblings;

    unsigned char data[sizeof(size_t)];
};

struct dpool_chunk {
    struct list_head siblings;
    size_t size, used;

    struct list_head all_allocations, free_allocations;

    unsigned char data[sizeof(size_t)];
};

#endif
