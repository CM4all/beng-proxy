/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_MOUNT_LIST_HXX
#define BENG_PROXY_MOUNT_LIST_HXX

#include "translation/Features.hxx"

#include <inline/compiler.h>

class AllocatorPtr;
class MatchInfo;

struct MountList {
    MountList *next;

    const char *source;
    const char *target;

#if TRANSLATION_ENABLE_EXPAND
    bool expand_source;
#endif

    bool writable;

    /**
     * Omit the MS_NOEXEC flag?
     */
    bool exec;

    constexpr MountList(const char *_source, const char *_target,
#if !TRANSLATION_ENABLE_EXPAND
                        gcc_unused
#endif
                        bool _expand_source=false, bool _writable=false,
                        bool _exec=false)
        :next(nullptr), source(_source), target(_target),
#if TRANSLATION_ENABLE_EXPAND
         expand_source(_expand_source),
#endif
         writable(_writable), exec(_exec) {
    }

    MountList(AllocatorPtr alloc, const MountList &src);

#if TRANSLATION_ENABLE_EXPAND
    bool IsExpandable() const {
        return expand_source;
    }

    gcc_pure
    static bool IsAnyExpandable(MountList *m) {
        for (; m != nullptr; m = m->next)
            if (m->IsExpandable())
                return true;

        return false;
    }

    void Expand(AllocatorPtr alloc, const MatchInfo &match_info);
    static void ExpandAll(AllocatorPtr alloc, MountList *m,
                          const MatchInfo &match_info);
#endif

    void Apply() const;

    static MountList *CloneAll(AllocatorPtr alloc, const MountList *src);
    static void ApplyAll(const MountList *m);
};

#endif
