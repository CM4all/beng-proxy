// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <sys/types.h>

struct pool;
class EventLoop;
class PipeStock;
class UnusedIstreamPtr;
class FileDescriptor;
class SharedLease;
namespace Uring { class Queue; }

/**
 * An #Istream implementation that uses io_uring to splice() data from
 * a file into a pipe, and the read end of the pipe gets submitted to
 * IstreamHandler::OnDirect().  This is only compatible with
 * #IstreamHandler implementations that allow "direct" I/O from pipes.
 *
 * This is useful to avoid sendfile() which can block the process if
 * disk (or network filesystem) I/O is slow.
 */
UnusedIstreamPtr
NewUringSpliceIstream(EventLoop &event_loop, Uring::Queue &uring, PipeStock *_pipe_stock,
		      struct pool &pool,
		      const char *path, FileDescriptor fd, SharedLease &&lease,
		      off_t start_offset, off_t end_offset) noexcept;
