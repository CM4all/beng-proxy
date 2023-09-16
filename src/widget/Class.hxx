// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "util/StringSet.hxx"

struct WidgetView;

/**
 * A widget class is a server which provides a widget.
 */
struct WidgetClass {
	/**
	 * A linked list of view descriptions.
	 */
	WidgetView *views = nullptr;

	/**
	 * The URI prefix that represents '@/'.
	 */
	const char *local_uri = nullptr;

	/**
	 * The (beng-proxy) hostname on which requests to this widget are
	 * allowed.  If not set, then this is a trusted widget.  Requests
	 * from an untrusted widget to a trusted one are forbidden.
	 */
	const char *untrusted_host = nullptr;

	/**
	 * The (beng-proxy) hostname prefix on which requests to this
	 * widget are allowed.  If not set, then this is a trusted widget.
	 * Requests from an untrusted widget to a trusted one are
	 * forbidden.
	 */
	const char *untrusted_prefix = nullptr;

	/**
	 * A hostname suffix on which requests to this widget are allowed.
	 * If not set, then this is a trusted widget.  Requests from an
	 * untrusted widget to a trusted one are forbidden.
	 */
	const char *untrusted_site_suffix = nullptr;

	/**
	 * @see @TRANSLATE_UNTRUSTED_RAW_SITE_SUFFIX
	 */
	const char *untrusted_raw_site_suffix = nullptr;

	const char *cookie_host = nullptr;

	/**
	 * The group name from #TRANSLATE_WIDGET_GROUP.  It is used to
	 * determine whether this widget may be embedded inside another
	 * one, see #TRANSLATE_GROUP_CONTAINER and #container_groups.
	 */
	const char *group = nullptr;

	/**
	 * If this list is non-empty, then this widget may only embed
	 * widgets from any of the specified groups.  The
	 * #TRANSLATE_SELF_CONTAINER flag adds an exception to this rule.
	 */
	StringSet container_groups;

	/**
	 * Does this widget support new-style direct URI addressing?
	 *
	 * Example: http://localhost/template.html;frame=foo/bar - this
	 * requests the widget "foo" and with path-info "/bar".
	 */
	bool direct_addressing = false;

	/** does beng-proxy remember the state (path_info and
	    query_string) of this widget? */
	bool stateful = false;

	/**
	 * @see #TranslationCommand::REQUIRE_CSRF_TOKEN
	 */
	bool require_csrf_token = false;

	/**
	 * Absolute URI paths are considered relative to the base URI of
	 * the widget.
	 */
	bool anchor_absolute = false;

	/**
	 * Send the "info" request headers to the widget?  See
	 * #TRANSLATE_WIDGET_INFO.
	 */
	bool info_headers = false;

	bool dump_headers = false;

	WidgetClass() = default;
	WidgetClass(AllocatorPtr alloc, const WidgetClass &src) noexcept;

	/**
	 * Determines whether it is allowed to embed the widget in a page with
	 * with the specified host name.  If not, throws a
	 * std::runtime_error with an explanatory message.
	 */
	void CheckHost(const char *host, const char *site_name) const;

	[[gnu::pure]]
	const WidgetView *FindViewByName(const char *name) const noexcept;

	[[gnu::pure]]
	bool HasGroups() const noexcept {
		return !container_groups.IsEmpty();
	}

	[[gnu::pure]]
	bool MayEmbed(const WidgetClass &child) const noexcept {
		return container_groups.IsEmpty() ||
			(child.group != nullptr &&
			 container_groups.Contains(child.group));
	}
};

extern const WidgetClass root_widget_class;
