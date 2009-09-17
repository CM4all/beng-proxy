/*
 * Child process management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WORKER_H
#define BENG_PROXY_WORKER_H

#include <inline/list.h>

#include <unistd.h>

struct worker {
    struct list_head siblings;

    struct instance *instance;

    pid_t pid;
};

pid_t
worker_new(struct instance *instance);

void
worker_killall(struct instance *instance);

#endif
