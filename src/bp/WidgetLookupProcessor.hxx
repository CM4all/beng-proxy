// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

struct pool;
template<typename T> class SharedPoolPtr;
struct WidgetContext;
class StopwatchPtr;
class UnusedIstreamPtr;
class Widget;
class WidgetLookupHandler;
class CancellablePointer;

/**
 * Process the specified istream, and find the specified widget.
 *
 * @param widget the widget that represents the template
 * @param id the id of the widget to be looked up
 */
void
processor_lookup_widget(struct pool &pool,
			const StopwatchPtr &parent_stopwatch,
			UnusedIstreamPtr istream,
			Widget &widget, const char *id,
			SharedPoolPtr<WidgetContext> ctx,
			unsigned options,
			WidgetLookupHandler &handler,
			CancellablePointer &cancel_ptr) noexcept;
