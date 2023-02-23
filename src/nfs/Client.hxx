// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <stdint.h>
#include <stddef.h>

struct pool;
class NfsClient;
class NfsClientHandler;
class NfsClientOpenFileHandler;
class NfsClientReadFileHandler;
class NfsFileHandle;
class CancellablePointer;
class EventLoop;

void
nfs_client_new(EventLoop &event_loop,
	       const char *server, const char *root,
	       NfsClientHandler &handler,
	       CancellablePointer &cancel_ptr) noexcept;

void
nfs_client_free(NfsClient *client) noexcept;

void
nfs_client_open_file(NfsClient &client,
		     const char *path,
		     NfsClientOpenFileHandler &handler,
		     CancellablePointer &cancel_ptr) noexcept;

void
nfs_client_close_file(NfsFileHandle &handle) noexcept;

void
nfs_client_read_file(NfsFileHandle &handle,
		     uint64_t offset, size_t length,
		     NfsClientReadFileHandler &handler) noexcept;
