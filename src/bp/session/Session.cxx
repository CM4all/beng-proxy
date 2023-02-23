// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Session.hxx"
#include "http/Address.hxx"
#include "pool/pool.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/StringAPI.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>
#include <string.h>

static constexpr std::chrono::seconds SESSION_TTL_NEW(120);

void
WidgetSession::Attach(Set &dest, Set &&src) noexcept
{
	src.clear_and_dispose([&dest](WidgetSession *other_ws){
		Set::insert_commit_data commit_data;
		auto [existing, inserted] = dest.insert(*other_ws);
		if (!inserted) {
			/* this WidgetSession exists already - attach
			   it (recursively) */
			existing->Attach(std::move(*other_ws));
			delete other_ws;
		}
	});
}

void
WidgetSession::Attach(WidgetSession &&src) noexcept
{
	Attach(children, std::move(src.children));

	/* the attached session is assumed to be more recent */
	if (src.path_info != nullptr || src.query_string != nullptr) {
		path_info = std::move(src.path_info);
		query_string = std::move(src.query_string);
	}
}

RealmSession::~RealmSession() noexcept
{
	widgets.clear_and_dispose(DeleteDisposer{});
}

void
RealmSession::Attach(RealmSession &&other) noexcept
{
	if (site == nullptr && other.site != nullptr)
		site = std::move(other.site);

	if (user == nullptr && other.user != nullptr) {
		user = std::move(other.user);
		user_expires = other.user_expires;
	}

	WidgetSession::Attach(widgets, std::move(other.widgets));

	cookies.MoveFrom(std::move(other.cookies));
}

Session::Session(SessionId _id, SessionId _csrf_salt) noexcept
	:id(_id), csrf_salt(_csrf_salt),
	 expires(Expiry::Touched(SESSION_TTL_NEW))
{
}

Session::~Session() noexcept
{
	realms.clear_and_dispose(DeleteDisposer{});
}

unsigned
Session::GetPurgeScore() const noexcept
{
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

bool
Session::IsAttach(std::span<const std::byte> other) const noexcept
{
	return attach.size() == other.size() &&
		memcmp(attach.data(), other.data(), attach.size()) == 0;
}

void
Session::Attach(Session &&other) noexcept
{
	if (other.expires > expires)
		expires = other.expires;

	++counter;

	if (other.cookie_received)
		cookie_received = true;

	if (translate == nullptr)
		translate = std::move(other.translate);

	if (language == nullptr)
		language = std::move(other.language);

	if (external_manager == nullptr && other.external_manager != nullptr) {
		external_manager_pool = std::move(other.external_manager_pool);
		external_manager = std::exchange(other.external_manager,
						 nullptr);
		external_keepalive = other.external_keepalive;
		next_external_keepalive = other.next_external_keepalive;
	}

	other.realms.clear_and_dispose([this](RealmSession *other_realm){
		RealmSessionSet::insert_commit_data hint;
		auto [existing, inserted] = realms.insert_check(*other_realm, hint);
		if (inserted) {
			/* doesn't exist already: create a copy (with
			   a new RealmSession::parent) and commit
			   it */
			auto *new_realm =
				new RealmSession(*this,
						 std::move(*other_realm));
			realms.insert_commit(*new_realm, hint);
		} else {
			/* exists already: attach */
			existing->Attach(std::move(*other_realm));
		}

		/* delete this RealmSession; we can't reuse it because
		   its "parent" field points to the moved-from Session
		   instance about to be deleted */
		delete other_realm;
	});
}

void
Session::SetTranslate(std::span<const std::byte> _translate) noexcept
{
	assert(_translate.data() != nullptr);

	if (translate.data() != nullptr &&
	    translate.size() == _translate.size() &&
	    memcmp(translate.data(), _translate.data(), _translate.size()) == 0)
		/* same value as before: no-op */
		return;

	translate = _translate;
}

bool
Session::SetRecover(const char *_recover) noexcept
{
	assert(_recover != nullptr);

	if (recover != nullptr && StringIsEqual(recover.c_str(), _recover))
		return false;

	recover = _recover;

	/* TODO: re-send session cookie with modified "recover" value
	   in all realms? */

	return true;
}

void
RealmSession::SetTranslate(std::span<const std::byte> _translate) noexcept
{
	assert(_translate.data() != nullptr);

	if (translate.data() != nullptr &&
	    translate.size() == _translate.size() &&
	    memcmp(translate.data(), _translate.data(), _translate.size()) == 0)
		/* same value as before: no-op */
		return;

	translate = _translate;
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

void
Session::SetExternalManager(const HttpAddress &address,
			    std::chrono::steady_clock::time_point now,
			    std::chrono::duration<uint16_t> keepalive) noexcept
{
	if (external_manager != nullptr) {
		delete external_manager;
		external_manager = nullptr;
		external_manager_pool.reset();
	} else {
		next_external_keepalive = std::chrono::steady_clock::time_point::min();
	}

	external_manager_pool =
		PoolPtr(pool_new_libc(nullptr,
				      "external_session_manager"));
	external_manager = new HttpAddress(*external_manager_pool,
					   address);
	external_keepalive = keepalive;

	/* assume the session is fresh now; postpone the first refresh
	   for one period */
	next_external_keepalive = now + keepalive;
}

static WidgetSession *
hashmap_r_get_widget_session(WidgetSession::Set &set,
			     const char *id, bool create)
{
	assert(id != nullptr);

	auto i = set.find(id, WidgetSession::Compare());
	if (i != set.end())
		return &*i;

	if (!create)
		return nullptr;

	auto *ws = new WidgetSession(id);
	set.insert(*ws);
	return ws;
}

WidgetSession *
RealmSession::GetWidget(const char *widget_id, bool create) noexcept
{
	assert(widget_id != nullptr);

	return hashmap_r_get_widget_session(widgets, widget_id, create);
}

WidgetSession *
WidgetSession::GetChild(const char *child_id, bool create) noexcept
{
	assert(child_id != nullptr);

	return hashmap_r_get_widget_session(children, child_id, create);
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
Session::GetRealm(const char *realm_name) noexcept
{
	RealmSessionSet::insert_commit_data commit_data;
	auto result = realms.insert_check(realm_name, RealmSession::Compare(),
					  commit_data);
	if (!result.second)
		return &*result.first;

	auto realm = new RealmSession(*this, realm_name);
	realms.insert_commit(*realm, commit_data);
	return realm;
}
