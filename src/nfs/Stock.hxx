// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>

class AllocatorPtr;
class NfsStock;
class NfsClient;
class CancellablePointer;
class EventLoop;

class NfsStockGetHandler {
public:
	virtual void OnNfsStockReady(NfsClient &client) noexcept = 0;
	virtual void OnNfsStockError(std::exception_ptr ep) noexcept = 0;
};

/**
 * NFS connection manager.
 */
NfsStock *
nfs_stock_new(EventLoop &event_loop) noexcept;

void
nfs_stock_free(NfsStock *stock) noexcept;

void
nfs_stock_get(NfsStock *stock, AllocatorPtr alloc,
	      const char *server, const char *export_name,
	      NfsStockGetHandler &handler,
	      CancellablePointer &cancel_ptr) noexcept;
