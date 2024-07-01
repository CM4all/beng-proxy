// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
class StockMap;
class HttpResponseHandler;
struct ChildOptions;
class EventLoop;
class CancellablePointer;

/*
 * This helper library glues delegate_glue and http_response_handler
 * together.
 */
void
delegate_stock_request(EventLoop &event_loop, StockMap &stock,
		       struct pool &pool,
		       const char *helper,
		       const ChildOptions &options,
		       const char *path, const char *content_type,
		       bool use_xattr,
		       HttpResponseHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept;
