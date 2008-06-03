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

void
children_deinit(void);

void
children_event_add(void);

void
children_event_del(void);

void
child_register(pid_t pid, child_callback_t callback, void *ctx);

#endif
