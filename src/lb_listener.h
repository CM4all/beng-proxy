/*
 * Listener on a TCP port.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_LISTENER_H
#define BENG_PROXY_LB_LISTENER_H

#include <inline/list.h>

#include <glib.h>

struct pool;

struct lb_listener {
    struct list_head siblings;

    struct pool *pool;

    struct lb_instance *instance;

    const struct lb_listener_config *config;

    struct listener *listener;
};

struct lb_listener *
lb_listener_new(struct lb_instance *instance,
                const struct lb_listener_config *config,
                GError **error_r);

void
lb_listener_free(struct lb_listener *listener);

#endif
