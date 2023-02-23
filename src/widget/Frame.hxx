// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Pick the output of a single widget for displaying it in an IFRAME.
 */

#pragma once

struct pool;
struct WidgetContext;
template<typename T> class SharedPoolPtr;
class Widget;
class HttpResponseHandler;
class WidgetLookupHandler;
class CancellablePointer;
class StopwatchPtr;

/**
 * Request the contents of the specified widget.  This is a wrapper
 * for widget_http_request() with some additional checks (untrusted
 * host, session management).
 */
void
frame_top_widget(struct pool &pool, Widget &widget,
		 SharedPoolPtr<WidgetContext> ctx,
		 const StopwatchPtr &parent_stopwatch,
		 HttpResponseHandler &_handler,
		 CancellablePointer &cancel_ptr);

/**
 * Looks up a child widget in the specified widget.  This is a wrapper
 * for widget_http_lookup() with some additional checks (untrusted
 * host, session management).
 */
void
frame_parent_widget(struct pool &pool, Widget &widget, const char *id,
		    SharedPoolPtr<WidgetContext> ctx,
		    const StopwatchPtr &parent_stopwatch,
		    WidgetLookupHandler &handler,
		    CancellablePointer &cancel_ptr);
