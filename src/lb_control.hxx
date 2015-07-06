/*
 * Listener on a control datagram socket.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_CONTROL_H
#define BENG_PROXY_LB_CONTROL_H

#include "glibfwd.hxx"

#include <inline/list.h>

struct lb_control_config;
struct ControlServer;

struct LbControl {
    struct list_head siblings;

    struct pool *pool;

    struct lb_instance *instance;

    ControlServer *server;
};

LbControl *
lb_control_new(struct lb_instance *instance,
               const struct lb_control_config *config,
               GError **error_r);

void
lb_control_free(LbControl *control);

void
lb_control_enable(LbControl *control);

void
lb_control_disable(LbControl *control);

#endif
