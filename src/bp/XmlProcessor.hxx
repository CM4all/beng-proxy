// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

/** options for processor_new() */
enum processor_options {
	/** rewrite URLs */
	PROCESSOR_REWRITE_URL = 0x1,

	/** add prefix to marked CSS class names */
	PROCESSOR_PREFIX_CSS_CLASS = 0x2,

	/**
	 * Default URI rewrite mode is base=widget mode=focus.
	 */
	PROCESSOR_FOCUS_WIDGET = 0x4,

	/** add prefix to marked XML ids */
	PROCESSOR_PREFIX_XML_ID = 0x8,

	/** enable the c:embed element */
	PROCESSOR_CONTAINER = 0x10,

	/**
	 * Invoke the CSS processor for "style" element contents?
	 */
	PROCESSOR_STYLE = 0x20,

	/**
	 * Allow this widget to embed more instances of its own class.
	 */
	PROCESSOR_SELF_CONTAINER = 0x40,
};

struct pool;
template<typename T> class SharedPoolPtr;
struct WidgetContext;
class StopwatchPtr;
class UnusedIstreamPtr;
class Widget;
class StringMap;

[[gnu::pure]]
bool
processable(const StringMap &headers) noexcept;

/**
 * Process the specified istream, and return the processed stream.
 *
 * @param widget the widget that represents the template
 */
UnusedIstreamPtr
processor_process(struct pool &pool,
		  const StopwatchPtr &parent_stopwatch,
		  UnusedIstreamPtr istream,
		  Widget &widget,
		  SharedPoolPtr<WidgetContext> ctx,
		  unsigned options) noexcept;
