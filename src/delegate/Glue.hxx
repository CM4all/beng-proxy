// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * This helper library glues delegate_stock and delegate_client
 * together.
 */

#pragma once

struct ChildOptions;
class StockMap;
class AllocatorPtr;
class DelegateHandler;
class CancellablePointer;

void
delegate_stock_open(StockMap *stock, AllocatorPtr alloc,
		    const char *helper,
		    const ChildOptions &options,
		    const char *path,
		    DelegateHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept;
