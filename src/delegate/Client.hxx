// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class EventLoop;
class SocketDescriptor;
class Lease;
class AllocatorPtr;
class CancellablePointer;
class DelegateHandler;

/**
 * Fork a process and delegate open() to it.  The subprocess returns
 * the file descriptor over a unix socket.
 *
 * @param fd the socket to the helper process
 */
void
delegate_open(EventLoop &event_loop, SocketDescriptor fd, Lease &lease,
	      AllocatorPtr alloc, const char *path,
	      DelegateHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept;
