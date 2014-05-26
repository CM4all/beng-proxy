/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_MOUNT_LIST_H
#define BENG_PROXY_MOUNT_LIST_H

#include <inline/compiler.h>

#include <stdbool.h>

struct pool;

struct mount_list {
    struct mount_list *next;

    const char *source;
    const char *target;
};

#ifdef __cplusplus
extern "C" {
#endif

struct mount_list *
mount_list_dup(struct pool *pool, const struct mount_list *src);

void
mount_list_apply(const struct mount_list *m);

#ifdef __cplusplus
}
#endif

#endif
