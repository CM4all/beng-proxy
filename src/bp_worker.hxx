/*
 * Child process management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WORKER_HXX
#define BENG_PROXY_WORKER_HXX

#include "crash.hxx"

#include <boost/intrusive/list.hpp>

#include <unistd.h>

struct BpInstance;

struct BpWorker
    : boost::intrusive::list_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

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
