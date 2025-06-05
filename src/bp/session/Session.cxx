// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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
	while (!src.empty()) {
		auto node = src.extract(src.begin());
		auto i = dest.insert(std::move(node));
		if (!i.inserted) {
			/* this WidgetSession exists already - attach
			   it (recursively) */
			i.position->second.Attach(std::move(i.node.mapped()));
		}
	}
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

RealmSession::~RealmSession() noexcept = default;

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

Session::~Session() noexcept = default;

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

	while (!other.realms.empty()) {
		auto node = other.realms.extract(other.realms.begin());

		auto existing = realms.find(node.key());
		if (existing == realms.end()) {
			// TODO optimize with lower_bound()
			/* doesn't exist already: create a copy (with
			   a new RealmSession::parent) and commit
			   it */
			realms.emplace(std::piecewise_construct,
				       std::forward_as_tuple(std::move(node.key())),
				       std::forward_as_tuple(*this, std::move(node.mapped())));
		} else {
			/* exists already: attach */
			existing->second.Attach(std::move(node.mapped()));
		}
	}
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
			     std::string_view id, bool create)
{
	if (auto i = set.find(id); i != set.end())
		return &i->second;

	if (!create)
		return nullptr;

	// TODO optimize, use hint with lower_bound()
	auto [it, inserted] = set.emplace(std::piecewise_construct,
					  std::forward_as_tuple(id),
					  std::forward_as_tuple());
	assert(inserted);
	return &it->second;
}

WidgetSession *
RealmSession::GetWidget(std::string_view widget_id, bool create) noexcept
{
	return hashmap_r_get_widget_session(widgets, widget_id, create);
}

WidgetSession *
WidgetSession::GetChild(std::string_view child_id, bool create) noexcept
{
	return hashmap_r_get_widget_session(children, child_id, create);
}

WidgetSession::~WidgetSession() noexcept = default;

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
	for (auto &[name, realm] : realms)
		realm.Expire(now);
}

RealmSession *
Session::GetRealm(std::string_view realm_name) noexcept
{
	if (auto i = realms.find(realm_name); i != realms.end())
		return &i->second;

	// TODO optimize, use hint with lower_bound()
	return &realms.emplace(realm_name, *this).first->second;
}

bool
Session::DiscardRealm(std::string_view realm) noexcept
{
	if (auto i = realms.find(realm); i != realms.end()) {
		realms.erase(i);
		return true;
	} else
		return false;
}
