/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_CHILD_H
#define __BENG_CHILD_H

#include <inline/compiler.h>

#include <sys/types.h>

struct pool;

typedef void (*child_callback_t)(int status, void *ctx);

#ifdef __cplusplus
extern "C" {
#endif

void
children_init(struct pool *pool);

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

/**
 * @param name a symbolic name for the process to be used in log
 * messages
 */
void
child_register(pid_t pid, const char *name,
               child_callback_t callback, void *ctx);

/**
 * Send a SIGTERM to a child process and unregister it.
 */
void
child_kill(pid_t pid);

/**
 * Returns the number of registered child processes.
 */
gcc_pure
unsigned
child_get_count(void);

#ifdef __cplusplus
}
#endif

#endif
