/*
 * Tiny linked list library.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_LIST_H
#define __BENG_LIST_H

#include "compiler.h"

#ifndef NDEBUG
#define LIST_POISON1  ((void *) 0x00100100)
#define LIST_POISON2  ((void *) 0x00200200)
#endif

struct list_head {
    struct list_head *prev, *next;
};

#define LIST_HEAD_INIT(name) { &(name), &(name) }

#define LIST_HEAD(name) \
	struct list_head name = LIST_HEAD_INIT(name)

static inline void
list_init(struct list_head *head)
{
    head->prev = head;
    head->next = head;
}

static inline void
list_add(struct list_head *new, struct list_head *head)
{
    new->next = head->next;
    new->prev = head;
    new->next->prev = new;
    head->next = new;
}

static inline void
list_remove(struct list_head *entry)
{
    entry->prev->next = entry->next;
    entry->next->prev = entry->prev;
#ifndef NDEBUG
    entry->next = LIST_POISON1;
    entry->prev = LIST_POISON2;
#endif
}

static inline int
list_empty(const struct list_head *head)
{
    return head->next == head;
}

#endif
