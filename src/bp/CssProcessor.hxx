// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

/** options for css_processor() */
enum css_processor_options {
	/** rewrite URLs */
	CSS_PROCESSOR_REWRITE_URL = 0x1,

	/** add prefix to marked CSS class names */
	CSS_PROCESSOR_PREFIX_CLASS = 0x2,

	/** add prefix to marked XML ids */
	CSS_PROCESSOR_PREFIX_ID = 0x4,
};

struct pool;
struct WidgetContext;
template<typename T> class SharedPoolPtr;
class StopwatchPtr;
class UnusedIstreamPtr;
class Widget;

/**
 * Process the specified istream, and return the processed stream.
 *
 * @param widget the widget that represents the template
 */
UnusedIstreamPtr
css_processor(struct pool &pool,
	      const StopwatchPtr &parent_stopwatch,
	      UnusedIstreamPtr stream,
	      Widget &widget,
	      SharedPoolPtr<WidgetContext> ctx,
	      unsigned options) noexcept;
