/*
 * Handler for control messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_HANDLER_HXX
#define BENG_PROXY_CONTROL_HANDLER_HXX

struct pool;
struct instance;

bool
global_control_handler_init(struct pool *pool, struct instance *instance);

void
global_control_handler_deinit(struct instance *instance);

void
global_control_handler_enable(struct instance &instance);

void
global_control_handler_disable(struct instance &instance);

/**
 * Creates a new socket for a child process which receives forwarded
 * control messages.
 *
 * @return the socket for the child process, or -1 on error
 */
int
global_control_handler_add_fd(struct instance *instance);

/**
 * Closes all sockets to child processes, and installs this socket
 * descriptor as source for control packets.  Call this after fork()
 * in the child processes.
 *
 * @param fd the new socket created before the fork() by
 * global_control_handler_add_fd()
 */
void
global_control_handler_set_fd(struct instance *instance, int fd);

void
local_control_handler_init(struct instance *instance);

void
local_control_handler_deinit(struct instance *instance);

bool
local_control_handler_open(struct instance *instance);

#endif
