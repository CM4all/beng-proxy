/*
 * Handler for control messages.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_CONTROL_HANDLER_HXX
#define BENG_PROXY_CONTROL_HANDLER_HXX

struct BpInstance;
class UniqueSocketDescriptor;

void
global_control_handler_init(BpInstance *instance);

void
global_control_handler_deinit(BpInstance *instance);

void
global_control_handler_enable(BpInstance &instance);

void
global_control_handler_disable(BpInstance &instance);

/**
 * Creates a new socket for a child process which receives forwarded
 * control messages.
 *
 * @return the socket for the child process, or -1 on error
 */
UniqueSocketDescriptor
global_control_handler_add_fd(BpInstance *instance);

/**
 * Closes all sockets to child processes, and installs this socket
 * descriptor as source for control packets.  Call this after fork()
 * in the child processes.
 *
 * @param fd the new socket created before the fork() by
 * global_control_handler_add_fd()
 */
void
global_control_handler_set_fd(BpInstance *instance,
                              UniqueSocketDescriptor &&fd);

void
local_control_handler_init(BpInstance *instance);

void
local_control_handler_deinit(BpInstance *instance);

void
local_control_handler_open(BpInstance *instance);

#endif
