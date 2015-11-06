/*
 * Child process management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WORKER_HXX
#define BENG_PROXY_WORKER_HXX

#include "crash.hxx"

#include <inline/list.h>

#include <unistd.h>

struct BpInstance;

struct worker {
    struct list_head siblings;

    BpInstance *instance;

    pid_t pid;

    struct crash crash;
};

pid_t
worker_new(BpInstance *instance);

void
worker_killall(BpInstance *instance);

#endif
