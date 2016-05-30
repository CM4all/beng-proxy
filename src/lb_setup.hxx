/*
 * Set up load balancer objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_SETUP_H
#define BENG_PROXY_LB_SETUP_H

struct LbInstance;
class Error;

bool
init_all_listeners(LbInstance &instance, Error &error_r);

void
deinit_all_listeners(LbInstance *instance);

void
all_listeners_event_add(LbInstance *instance);

void
all_listeners_event_del(LbInstance *instance);

void
init_all_controls(LbInstance *instance);

void
deinit_all_controls(LbInstance *instance);

void
enable_all_controls(LbInstance *instance);

#endif
