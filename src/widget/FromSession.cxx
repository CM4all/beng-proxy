/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Widget.hxx"
#include "Class.hxx"
#include "bp/session/Session.hxx"

#include <assert.h>

void
Widget::SaveToSession(WidgetSession &ws) const noexcept
try {
	assert(cls != nullptr);
	assert(cls->stateful); /* cannot save state for stateless widgets */

	auto &p = ws.session.parent.pool;

	ws.path_info.Set(p, from_request.path_info);

	if (from_request.query_string.empty())
		ws.query_string.Clear(p);
	else
		ws.query_string.Set(p, from_request.query_string);
} catch (const std::bad_alloc &) {
}

void
Widget::LoadFromSession(const WidgetSession &ws) noexcept
{
	assert(cls != nullptr);
	assert(cls->stateful); /* cannot load state from stateless widgets */
	assert(lazy.address == nullptr);

	from_request.path_info = ws.path_info;

	if (ws.query_string != nullptr)
		from_request.query_string = ws.query_string.c_str();
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
