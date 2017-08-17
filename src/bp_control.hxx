/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * Handler for control messages.
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
