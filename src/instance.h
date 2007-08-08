/*
 * Global declarations.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_INSTANCE_H
#define __BENG_INSTANCE_H

#include "listener.h"
#include "list.h"

struct instance {
    pool_t pool;
    listener_t listener;
    struct list_head connections;
    int should_exit;
    struct event sigterm_event, sigint_event, sigquit_event;
};

#endif

