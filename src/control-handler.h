/*
 * Handler for control messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_HANDLER_H
#define BENG_PROXY_CONTROL_HANDLER_H

#include "pool.h"

#include <stdbool.h>

struct instance;

bool
global_control_handler_init(pool_t pool, struct instance *instance);

void
global_control_handler_deinit(void);

#endif
