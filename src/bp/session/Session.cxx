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

#include "Session.hxx"
#include "http/Address.hxx"
#include "pool/pool.hxx"
#include "util/DeleteDisposer.hxx"
#include "AllocatorPtr.hxx"

#include <boost/intrusive/unordered_set.hpp>

#include <assert.h>
#include <string.h>

static constexpr std::chrono::seconds SESSION_TTL_NEW(120);

Session::Session(SessionId _id) noexcept
	:id(_id),
	 expires(Expiry::Touched(SESSION_TTL_NEW))
{
	csrf_salt.Generate();
}

Session::~Session() noexcept
{
	realms.clear_and_dispose(DeleteDisposer{});
}

unsigned
Session::GetPurgeScore() const noexcept
{
	if (is_new)
		return 1000;

	if (!cookie_received)
		return 50;

	if (!HasUser())
		return 20;

	return 1;
}

void
Session::ClearTranslate() noexcept
{
	translate = nullptr;
}

void
Session::SetTranslate(ConstBuffer<void> _translate)
{
	assert(!_translate.IsNull());

	if (!translate.IsNull() &&
	    translate.size() == _translate.size &&
	    memcmp(translate.data(), _translate.data, _translate.size) == 0)
		/* same value as before: no-op */
		return;

	translate = ConstBuffer<std::byte>::FromVoid(_translate);
}

void
RealmSession::SetUser(const char *_user, std::chrono::seconds max_age) noexcept
{
	assert(_user != nullptr);

	user = _user;

	if (max_age < std::chrono::seconds::zero())
		/* never expires */
		user_expires = Expiry::Never();
	else if (max_age == std::chrono::seconds::zero())
		/* expires immediately, use only once */
		user_expires = Expiry::AlreadyExpired();
	else
		user_expires.Touch(max_age);
}

bool
Session::SetExternalManager(const HttpAddress &address,
			    std::chrono::steady_clock::time_point now,
			    std::chrono::duration<uint16_t> keepalive)
{
	if (external_manager != nullptr) {
		delete external_manager;
		external_manager = nullptr;
		external_manager_pool.reset();
	} else {
		next_external_keepalive = std::chrono::steady_clock::time_point::min();
	}

	try {
		external_manager_pool =
			PoolPtr(pool_new_libc(nullptr,
					      "external_session_manager"));
		external_manager = new HttpAddress(*external_manager_pool,
						   address);
		external_keepalive = keepalive;

		/* assume the session is fresh now; postpone the first refresh
		   for one period */
		next_external_keepalive = now + keepalive;

		return true;
	} catch (const std::bad_alloc &) {
		return false;
	}
}

static WidgetSession *
hashmap_r_get_widget_session(RealmSession &session, WidgetSession::Set &set,
			     const char *id, bool create)
{
	assert(id != nullptr);

	auto i = set.find(id, WidgetSession::Compare());
	if (i != set.end())
		return &*i;

	if (!create)
		return nullptr;

	auto *ws = new WidgetSession(session, id);
	set.insert(*ws);
	return ws;
}

WidgetSession *
RealmSession::GetWidget(const char *widget_id, bool create)
try {
	assert(widget_id != nullptr);

	return hashmap_r_get_widget_session(*this, widgets, widget_id, create);
} catch (const std::bad_alloc &) {
	return nullptr;
}

WidgetSession *
WidgetSession::GetChild(const char *child_id, bool create)
try {
	assert(child_id != nullptr);

	return hashmap_r_get_widget_session(session, children, child_id, create);
} catch (const std::bad_alloc &) {
	return nullptr;
}

WidgetSession::~WidgetSession() noexcept
{
	children.clear_and_dispose(DeleteDisposer{});
}

void
RealmSession::Expire(Expiry now) noexcept
{
	if (user != nullptr && user_expires.IsExpired(now))
		ClearUser();

	cookies.Expire(now);
}

void
Session::Expire(Expiry now) noexcept
{
	for (auto &realm : realms)
		realm.Expire(now);
}

RealmSession *
Session::GetRealm(const char *realm_name)
try {
	RealmSessionSet::insert_commit_data commit_data;
	auto result = realms.insert_check(realm_name, RealmSession::Compare(),
					  commit_data);
	if (!result.second)
		return &*result.first;

	auto realm = new RealmSession(*this, realm_name);
	realms.insert_commit(*realm, commit_data);
	return realm;
} catch (const std::bad_alloc &) {
	return nullptr;
}
