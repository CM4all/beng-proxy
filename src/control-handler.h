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

/**
 * Creates a new socket for a child process which receives forwarded
 * control messages.
 *
 * @return the socket for the child process, or -1 on error
 */
int
global_control_handler_add_fd(void);

/**
 * Closes all sockets to child processes, and installs this socket
 * descriptor as source for control packets.  Call this after fork()
 * in the child processes.
 *
 * @param fd the new socket created before the fork() by
 * global_control_handler_add_fd()
 */
void
global_control_handler_set_fd(int fd);

#endif
