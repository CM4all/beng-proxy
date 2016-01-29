/*
 * Set up load balancer objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_SETUP_H
#define BENG_PROXY_LB_SETUP_H

struct lb_instance;
class Error;

bool
init_all_listeners(struct lb_instance &instance, Error &error_r);

void
deinit_all_listeners(struct lb_instance *instance);

void
all_listeners_event_add(struct lb_instance *instance);

void
all_listeners_event_del(struct lb_instance *instance);

void
init_all_controls(struct lb_instance *instance);

void
deinit_all_controls(struct lb_instance *instance);

void
enable_all_controls(struct lb_instance *instance);

#endif
