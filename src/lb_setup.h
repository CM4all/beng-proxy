/*
 * Set up load balancer objects.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LB_SETUP_H
#define BENG_PROXY_LB_SETUP_H

#include <stdbool.h>

struct lb_instance;

bool
init_all_listeners(struct lb_instance *instance);

void
deinit_all_listeners(struct lb_instance *instance);

#endif
