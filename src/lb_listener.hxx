/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_LISTENER_H
#define BENG_PROXY_LB_LISTENER_H

#include "gerror.h"

#include <inline/list.h>

struct pool;

struct lb_listener {
    struct list_head siblings;

    struct pool *pool;

    struct lb_instance *instance;

    const struct lb_listener_config *config;

    struct ssl_factory *ssl_factory;

    struct notify *notify;

    struct listener *listener;
};

struct lb_listener *
lb_listener_new(struct lb_instance *instance,
                const struct lb_listener_config *config,
                GError **error_r);

void
lb_listener_free(struct lb_listener *listener);

void
lb_listener_event_add(struct lb_listener *listener);

void
lb_listener_event_del(struct lb_listener *listener);

#endif
