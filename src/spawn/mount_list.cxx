/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "mount_list.hxx"
#include "system/bind_mount.h"
#include "AllocatorPtr.hxx"

#if TRANSLATION_ENABLE_EXPAND
#include "pexpand.hxx"
#endif

#include <sys/mount.h>
#include <unistd.h>
#include <string.h>
#include <stdio.h>
#include <errno.h>

inline
MountList::MountList(AllocatorPtr alloc, const MountList &src)
    :next(nullptr),
     source(alloc.Dup(src.source)),
     target(alloc.Dup(src.target)),
#if TRANSLATION_ENABLE_EXPAND
     expand_source(src.expand_source),
#endif
     writable(src.writable),
     exec(src.exec) {}

MountList *
MountList::CloneAll(AllocatorPtr alloc, const MountList *src)
{
    MountList *head = nullptr, **tail = &head;

    for (; src != nullptr; src = src->next) {
        MountList *dest = alloc.New<MountList>(alloc, *src);
        *tail = dest;
        tail = &dest->next;
    }

    return head;
}

#if TRANSLATION_ENABLE_EXPAND

void
MountList::Expand(AllocatorPtr alloc, const MatchInfo &match_info)
{
    if (expand_source) {
        expand_source = false;

        source = expand_string_unescaped(alloc, source, match_info);
    }
}

void
MountList::ExpandAll(AllocatorPtr alloc, MountList *m,
                     const MatchInfo &match_info)
{
    for (; m != nullptr; m = m->next)
        m->Expand(alloc, match_info);
}

#endif

inline void
MountList::Apply() const
{
    int flags = MS_NOSUID|MS_NODEV;
    if (!writable)
        flags |= MS_RDONLY;
    if (!exec)
        flags |= MS_NOEXEC;

    bind_mount(source, target, flags);
}

void
MountList::ApplyAll(const MountList *m)
{
    for (; m != nullptr; m = m->next)
        m->Apply();
}
