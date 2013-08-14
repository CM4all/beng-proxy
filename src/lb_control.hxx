/*
 * Listener on a control datagram socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONTROL_H
#define BENG_PROXY_LB_CONTROL_H

#include <inline/list.h>

#include <glib.h>

struct lb_control_config;

struct lb_control {
    struct list_head siblings;

    struct pool *pool;

    struct lb_instance *instance;

    struct control_server *server;
};

struct lb_control *
lb_control_new(struct lb_instance *instance,
               const struct lb_control_config *config,
               GError **error_r);

void
lb_control_free(struct lb_control *control);

void
lb_control_enable(struct lb_control *control);

void
lb_control_disable(struct lb_control *control);

#endif
