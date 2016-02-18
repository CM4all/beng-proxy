/*
 * Central manager for child processes.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CHILD_MANAGER_H
#define BENG_PROXY_CHILD_MANAGER_H

#include <inline/compiler.h>

#include <sys/types.h>

class ExitListener;

void
children_init();

void
children_deinit();

/**
 * Forget all registered children.  Call this in the new child process
 * after forking.
 */
void
children_clear();

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
               ExitListener *listener);

/**
 * Send a signal to a child process and unregister it.
 */
void
child_kill_signal(pid_t pid, int signo);

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

#endif
