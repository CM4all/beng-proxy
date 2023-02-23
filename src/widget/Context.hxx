// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "Ptr.hxx"
#include "bp/session/Id.hxx"
#include "util/IntrusiveForwardList.hxx"

#include <string_view>

class EventLoop;
class ResourceLoader;
class WidgetRegistry;
class StringMap;
class SessionManager;
class SessionLease;
class RealmSessionLease;
class Widget;
class AllocatorPtr;
struct HeaderForwardSettings;

struct WidgetContext {
	EventLoop &event_loop;

	ResourceLoader &resource_loader;
	ResourceLoader &filter_resource_loader;

	WidgetRegistry *widget_registry;

	const char *site_name;

	/**
	 * If non-NULL, then only untrusted widgets with this host are
	 * allowed; all trusted widgets are rejected.
	 */
	const char *untrusted_host;

	const char *local_host;
	const char *remote_host;

	const char *peer_subject = nullptr, *peer_issuer_subject = nullptr;

	/**
	 * The authenticated user, for generating the
	 * "X-CM4all-BENG-User" request header.
	 */
	const char *user = nullptr;

	const char *uri;

	const char *absolute_uri;

	/** the base URI which was requested by the beng-proxy client */
	std::string_view external_base_uri;

	/** semicolon-arguments in the external URI */
	const StringMap *args;

	const StringMap *request_headers;

	SessionManager *session_manager;

	/**
	 * The name of the session cookie.
	 */
	const char *session_cookie;

	SessionId session_id;
	const char *realm;

	IntrusiveForwardList<Widget> root_widgets;

	WidgetContext(EventLoop &_event_loop,
		      ResourceLoader &_resource_loader,
		      ResourceLoader &_filter_resource_loader,
		      WidgetRegistry *_widget_registry,
		      const char *site_name,
		      const char *untrusted_host,
		      const char *local_host,
		      const char *remote_host,
		      const char *request_uri,
		      const char *absolute_uri,
		      std::string_view external_base_uri,
		      const StringMap *args,
		      SessionManager *session_manager,
		      const char *session_cookie,
		      SessionId session_id,
		      const char *realm,
		      const StringMap *request_headers);

	~WidgetContext() noexcept;

	SessionLease GetSession() const;
	RealmSessionLease GetRealmSession() const;

	Widget &AddRootWidget(WidgetPtr widget) noexcept;

	StringMap ForwardRequestHeaders(AllocatorPtr alloc,
					bool exclude_host,
					bool with_body,
					bool forward_charset,
					bool forward_encoding,
					bool forward_range,
					const HeaderForwardSettings &settings,
					const char *host_and_port,
					const char *uri) noexcept;
};
