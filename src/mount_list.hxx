/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_MOUNT_LIST_HXX
#define BENG_PROXY_MOUNT_LIST_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;

struct MountList {
    MountList *next;

    const char *source;
    const char *target;

    bool expand_source;

    constexpr MountList(const char *_source, const char *_target,
                        bool _expand_source)
        :next(nullptr), source(_source), target(_target),
         expand_source(_expand_source) {}

    MountList(struct pool &pool, const MountList &src);

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

    bool Expand(struct pool &pool, const GMatchInfo *match_info,
                GError **error_r);
    static bool ExpandAll(struct pool &pool, MountList *m,
                          const GMatchInfo *match_info, GError **error_r);

    void Apply() const;

    static MountList *CloneAll(struct pool &pool, const MountList *src);
    static void ApplyAll(const MountList *m);
};

#endif
