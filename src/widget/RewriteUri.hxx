// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Rewrite URIs in templates.
 */

#pragma once

#include <string_view>

struct pool;
struct WidgetContext;
template<typename T> class SharedPoolPtr;
class StopwatchPtr;
class UnusedIstreamPtr;
class Widget;
struct escape_class;

enum class RewriteUriMode {
	DIRECT,
	FOCUS,
	PARTIAL,

	/**
	 * Embed the widget's HTTP response instead of generating an URI
	 * to the widget server.
	 */
	RESPONSE,
};

[[gnu::pure]]
RewriteUriMode
parse_uri_mode(std::string_view s) noexcept;

/**
 * @param untrusted_host the value of the UNTRUSTED translation
 * packet, or NULL if this is a "trusted" request
 * @param stateful if true, then the current request/session state is
 * taken into account (path_info and query_string)
 * @param view the name of a view, or NULL to use the default view
 */
UnusedIstreamPtr
rewrite_widget_uri(struct pool &pool,
		   SharedPoolPtr<WidgetContext> ctx,
		   const StopwatchPtr &parent_stopwatch,
		   Widget &widget,
		   std::string_view value,
		   RewriteUriMode mode, bool stateful,
		   const char *view,
		   const struct escape_class *escape) noexcept;
