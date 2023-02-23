// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Context.hxx"
#include "bp/session/Lease.hxx"
#include "bp/session/Manager.hxx"

SessionLease
WidgetContext::GetSession() const
{
	if (session_manager == nullptr || !session_id.IsDefined())
		return nullptr;

	return {*session_manager, session_id};
}

RealmSessionLease
WidgetContext::GetRealmSession() const
{
	return RealmSessionLease(GetSession(), realm);
}
