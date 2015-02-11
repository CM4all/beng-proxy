/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_MOUNT_LIST_HXX
#define BENG_PROXY_MOUNT_LIST_HXX

struct pool;

struct MountList {
    MountList *next;

    const char *source;
    const char *target;

    MountList(const char *_source, const char *_target)
        :next(nullptr), source(_source), target(_target) {}
};

MountList *
mount_list_dup(struct pool *pool, const MountList *src);

void
mount_list_apply(const MountList *m);

#endif
