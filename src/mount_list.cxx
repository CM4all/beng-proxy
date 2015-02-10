/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "mount_list.hxx"
#include "bind_mount.h"
#include "pool.hxx"
#include "regex.hxx"

#include <sys/mount.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

inline
MountList::MountList(struct pool &pool, const MountList &src)
    :next(nullptr),
     source(p_strdup(&pool, src.source)),
     target(p_strdup(&pool, src.target)),
     expand_source(src.expand_source) {}

MountList *
MountList::CloneAll(struct pool &pool, const MountList *src)
{
    MountList *head = nullptr, **tail = &head;

    for (; src != nullptr; src = src->next) {
        MountList *dest = NewFromPool<MountList>(pool, pool, *src);
        *tail = dest;
        tail = &dest->next;
    }

    return head;
}

bool
MountList::Expand(struct pool &pool, const GMatchInfo *match_info,
                  GError **error_r)
{
    if (expand_source) {
        expand_source = false;

        source = expand_string_unescaped(&pool, source, match_info, error_r);
        if (source == nullptr)
            return false;
    }

    return true;
}

bool
MountList::ExpandAll(struct pool &pool, MountList *m,
                     const GMatchInfo *match_info, GError **error_r)
{
    for (; m != nullptr; m = m->next)
        if (!m->Expand(pool, match_info, error_r))
            return false;

    return true;
}

inline void
MountList::Apply() const
{
    bind_mount(source, target, MS_NOEXEC|MS_NOSUID|MS_NODEV|MS_RDONLY);
}

void
MountList::ApplyAll(const MountList *m)
{
    for (; m != nullptr; m = m->next)
        m->Apply();
}
