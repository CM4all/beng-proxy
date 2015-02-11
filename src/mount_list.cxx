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

inline
MountList::MountList(struct pool &pool, const MountList &src)
    :next(nullptr),
     source(p_strdup(&pool, src.source)),
     target(p_strdup(&pool, src.target)) {}

static MountList *
mount_list_dup_one(struct pool *pool, const MountList *src)
{
    return NewFromPool<MountList>(*pool, *pool, *src);
}

MountList *
mount_list_dup(struct pool *pool, const MountList *src)
{
    MountList *head = nullptr, **tail = &head;

    for (; src != nullptr; src = src->next) {
        MountList *dest = mount_list_dup_one(pool, src);
        *tail = dest;
        tail = &dest->next;
    }

    return head;
}

static void
mount_list_apply_one(const MountList *m)
{
    bind_mount(m->source, m->target, MS_NOEXEC|MS_NOSUID|MS_NODEV|MS_RDONLY);
}

void
mount_list_apply(const MountList *m)
{
    for (; m != nullptr; m = m->next)
        mount_list_apply_one(m);
}
