/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CHILD_H
#define __BENG_CHILD_H

#include "pool.h"

#include <sys/types.h>

typedef void (*child_callback_t)(int status, void *ctx);

void
children_init(pool_t pool);

/**
 * Begin shutdown of this subsystem: wait for all children to exit,
 * and then remove the event.
 */
void
children_shutdown(void);

void
children_event_add(void);

void
children_event_del(void);

void
child_register(pid_t pid, child_callback_t callback, void *ctx);

#endif
