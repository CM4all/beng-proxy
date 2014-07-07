/*
 * Set up load balancer objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_SETUP_H
#define BENG_PROXY_LB_SETUP_H

#include "glibfwd.hxx"

struct lb_instance;

bool
init_all_listeners(struct lb_instance &instance, GError **error_r);

void
deinit_all_listeners(struct lb_instance *instance);

void
all_listeners_event_add(struct lb_instance *instance);

void
all_listeners_event_del(struct lb_instance *instance);

bool
init_all_controls(struct lb_instance *instance, GError **error_r);

void
deinit_all_controls(struct lb_instance *instance);

void
enable_all_controls(struct lb_instance *instance);

#endif
