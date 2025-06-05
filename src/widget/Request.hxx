// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct pool;
class Widget;
struct WidgetContext;
template<typename T> class SharedPoolPtr;
class StopwatchPtr;
class HttpResponseHandler;
class CancellablePointer;
class WidgetLookupHandler;

/**
 * Sends a HTTP request to the widget, apply all transformations, and
 * return the result to the #http_response_handler.
 */
void
widget_http_request(struct pool &pool, Widget &widget,
		    SharedPoolPtr<WidgetContext> ctx,
		    const StopwatchPtr &parent_stopwatch,
		    HttpResponseHandler &handler,
		    CancellablePointer &cancel_ptr) noexcept;

/**
 * Send a HTTP request to the widget server, process it, and look up
 * the specified widget in the processed result.
 *
 * @param widget the widget that represents the template
 * @param id the id of the widget to be looked up
 */
void
widget_http_lookup(struct pool &pool, Widget &widget, const char *id,
		   SharedPoolPtr<WidgetContext> ctx,
		   const StopwatchPtr &parent_stopwatch,
		   WidgetLookupHandler &handler,
		   CancellablePointer &cancel_ptr) noexcept;
