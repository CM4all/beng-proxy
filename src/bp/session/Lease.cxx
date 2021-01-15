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

/*
 * Session management.
 */

#include "Lease.hxx"
#include "Session.hxx"
#include "Manager.hxx"

SessionLease::SessionLease(SessionManager &_manager, SessionId id) noexcept
	:SessionLease(_manager.Find(id)) {}

void
SessionLease::Put(SessionManager &manager, Session &session) noexcept
{
	manager.Put(session);
}

RealmSessionLease::RealmSessionLease(SessionLease &&src, const char *realm) noexcept
	:session(src.session != nullptr
		 ? src.session->GetRealm(realm)
		 : nullptr),
	 manager(src.manager)
{
	if (session != nullptr)
		src.session = nullptr;
}

RealmSessionLease::RealmSessionLease(SessionManager &_manager,
				     SessionId id, const char *realm) noexcept
	:manager(&_manager)
{
	SessionLease parent(_manager, id);
	if (parent)
		return;

	session = parent.session->GetRealm(realm);
	if (session != nullptr)
		parent.session = nullptr;
}

void
RealmSessionLease::Put(SessionManager &manager, RealmSession &session) noexcept
{
	manager.Put(session.parent);
}
