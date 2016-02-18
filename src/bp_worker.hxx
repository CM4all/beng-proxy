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

struct BpWorker {
    struct list_head siblings;

    BpInstance &instance;

    const pid_t pid;

    struct crash crash;

    BpWorker(BpInstance &_instance, pid_t _pid,
             const struct crash &_crash)
        :instance(_instance), pid(_pid), crash(_crash) {}

    ~BpWorker() {
        crash_deinit(&crash);
    }
};

pid_t
worker_new(BpInstance *instance);

void
worker_killall(BpInstance *instance);

#endif
