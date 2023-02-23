// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Widget.hxx"
#include "bp/session/Session.hxx"
#include "pool/tpool.hxx"

#include <assert.h>

WidgetSession *
Widget::GetSession(RealmSession &session, bool create) noexcept
{
	if (id == nullptr)
		return nullptr;

	if (parent == nullptr)
		return session.GetWidget(id, create);

	switch (session_scope) {
	case Widget::SessionScope::RESOURCE:
		/* the session is bound to the resource: determine
		   widget_session from the parent's session */

		{
			auto *parent_session = parent->GetSession(session, create);
			if (parent_session == nullptr)
				return nullptr;

			const TempPoolLease tpool;
			return parent_session->GetChild(id, create);
		}

	case Widget::SessionScope::SITE:
		/* this is a site-global widget: get the widget_session
		   directly from the session struct (which is site
		   specific) */

		{
			const TempPoolLease tpool;
			return session.GetWidget(id, create);
		}
	}

	assert(0);
	return nullptr;
}
