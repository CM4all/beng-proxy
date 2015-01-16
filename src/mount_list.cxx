/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "mount_list.hxx"
#include "bind_mount.h"
#include "pool.hxx"

#include <sys/mount.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

static struct mount_list *
mount_list_dup_one(struct pool *pool, const struct mount_list *src)
{
    auto *dest = NewFromPool<struct mount_list>(*pool);
    dest->next = nullptr;
    dest->source = p_strdup(pool, src->source);
    dest->target = p_strdup(pool, src->target);
    return dest;
}

struct mount_list *
mount_list_dup(struct pool *pool, const struct mount_list *src)
{
    struct mount_list *head = nullptr, **tail = &head;

    for (; src != nullptr; src = src->next) {
        struct mount_list *dest = mount_list_dup_one(pool, src);
        *tail = dest;
        tail = &dest->next;
    }

    return head;
}

static void
mount_list_apply_one(const struct mount_list *m)
{
    bind_mount(m->source, m->target, MS_NOEXEC|MS_NOSUID|MS_NODEV|MS_RDONLY);
}

void
mount_list_apply(const struct mount_list *m)
{
    for (; m != nullptr; m = m->next)
        mount_list_apply_one(m);
}
