// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Widget.hxx"
#include "Class.hxx"
#include "bp/session/Session.hxx"

#include <assert.h>

void
Widget::SaveToSession(WidgetSession &ws) const noexcept
{
	assert(cls != nullptr);
	assert(cls->stateful); /* cannot save state for stateless widgets */

	ws.path_info = from_request.path_info;

	if (from_request.query_string.empty())
		ws.query_string = nullptr;
	else
		ws.query_string = from_request.query_string;
}

void
Widget::LoadFromSession(const WidgetSession &ws) noexcept
{
	assert(cls != nullptr);
	assert(cls->stateful); /* cannot load state from stateless widgets */
	assert(lazy.address == nullptr);

	from_request.path_info = ws.path_info.c_str();
	from_request.query_string = (std::string_view)ws.query_string;
}

void
Widget::LoadFromSession(RealmSession &session) noexcept
{
	assert(parent != nullptr);
	assert(lazy.address == nullptr);
	assert(cls != nullptr);
	assert(cls->stateful);
	assert(session_sync_pending);
	assert(!session_save_pending);

	session_sync_pending = false;

	if (!ShouldSyncSession())
		/* not stateful in this request */
		return;

	/* are we focused? */

	if (HasFocus()) {
		/* postpone until we have the widget's response; we do not
		   know yet which view will be used until we have checked the
		   response headers */

		session_save_pending = true;
	} else {
		/* get query string from session */

		auto *ws = GetSession(session, false);
		if (ws != nullptr)
			LoadFromSession(*ws);
	}
}

void
Widget::SaveToSession(RealmSession &session) noexcept
{
	assert(parent != nullptr);
	assert(cls != nullptr);
	assert(cls->stateful);
	assert(!session_sync_pending);
	assert(session_save_pending);

	session_save_pending = false;

	if (!ShouldSyncSession())
		/* not stateful in this request */
		return;

	auto *ws = GetSession(session, true);
	if (ws != nullptr)
		SaveToSession(*ws);
}
