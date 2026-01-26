// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Manager.hxx"
#include "Lease.hxx"
#include "io/Logger.hxx"
#include "system/Seed.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/StaticVector.hxx"
#include "util/PrintException.hxx"

#include <cassert>
#include <cstring> // for memcmp()

static constexpr unsigned MAX_SESSIONS = 65536;

inline const SessionId &
SessionManager::SessionGetId::operator()(const Session &session) const noexcept
{
	return session.id;
}

inline std::span<const std::byte>
SessionManager::SessionGetAttach::operator()(const Session &session) const noexcept
{
	return session.attach;
}

inline bool
SessionManager::SessionAttachEqual::operator()(std::span<const std::byte> a,
					       std::span<const std::byte> b) const noexcept
{
	return a.size() == b.size() && memcmp(a.data(), b.data(), a.size()) == 0;
}

void
SessionManager::EraseAndDispose(Session &session)
{
	assert(!sessions.empty());

	auto i = sessions.iterator_to(session);
	sessions.erase_and_dispose(i, DeleteDisposer{});
}

void
SessionManager::Cleanup() noexcept
{
	const Expiry now = Expiry::Now();

	sessions.remove_and_dispose_if([now](const Session &session){
		return session.expires.IsExpired(now);
	}, DeleteDisposer{});

	if (!sessions.empty())
		cleanup_timer.Schedule(cleanup_interval);

	try {
		/* reseed the session id generator every few minutes;
		   this isn't about cleanup, but this timer is a good
		   hook for calling it */
		SeedPrng();
	} catch (...) {
		PrintException(std::current_exception());
	}
}

SessionManager::SessionManager(EventLoop &event_loop,
			       std::chrono::seconds _idle_timeout,
			       unsigned _cluster_size,
			       unsigned _cluster_node) noexcept
	:cluster_size(_cluster_size), cluster_node(_cluster_node),
	 idle_timeout(_idle_timeout),
	 prng(MakeSeeded<SessionPrng>()),
	 cleanup_timer(event_loop, BIND_THIS_METHOD(Cleanup))
{
}

void
SessionManager::SeedPrng()
{
	auto ss = GenerateSeedSeq<SessionPrng>();
	prng.seed(ss);
}

SessionManager::~SessionManager() noexcept
{
	sessions.clear_and_dispose(DeleteDisposer{});
}

void
SessionManager::AdjustNewSessionId(SessionId &id) const noexcept
{
	if (cluster_size > 0)
		id.SetClusterNode(cluster_size, cluster_node);
}

void
SessionManager::Insert(Session &session) noexcept
{
	sessions.insert(session);

	if (!cleanup_timer.IsPending())
		cleanup_timer.Schedule(cleanup_interval);
}

bool
SessionManager::Purge() noexcept
{
	/* collect at most 256 sessions */
	StaticVector<std::reference_wrapper<Session>, 256> purge_sessions;
	unsigned highest_score = 0;

	sessions.for_each([&purge_sessions, &highest_score](Session &session) {
		unsigned score = session.GetPurgeScore();
		if (score > highest_score) {
			purge_sessions.clear();
			highest_score = score;
		}

		if (score == highest_score && !purge_sessions.full())
			purge_sessions.emplace_back(session);
	});

	if (purge_sessions.empty())
		return false;

	LogConcat(3, "SessionManager", "purging ", (unsigned)purge_sessions.size(),
		  " sessions (score=", highest_score, ")");

	for (auto &session : purge_sessions) {
		EraseAndDispose(session);
	}

	/* purge again if the highest score group has only very few items,
	   which would lead to calling this (very expensive) function too
	   often */
	bool again = purge_sessions.size() < 16 &&
					     Count() > MAX_SESSIONS - 256;
	if (again)
		Purge();

	return true;
}

inline SessionId
SessionManager::GenerateSessionId() noexcept
{
	SessionId id;
	id.Generate(prng);
	AdjustNewSessionId(id);
	return id;
}

SessionLease
SessionManager::CreateSession() noexcept
{
	if (Count() >= MAX_SESSIONS)
		Purge();

	SessionId csrf_salt;
	csrf_salt.Generate(prng);

	Session *session = new Session(GenerateSessionId(), csrf_salt);
	Insert(*session);
	return {*this, session};
}

SessionLease
SessionManager::Find(SessionId id) noexcept
{
	if (!id.IsDefined())
		return nullptr;

	auto i = sessions.find(id);
	if (i == sessions.end())
		return nullptr;

	Session &session = *i;

	session.expires.Touch(idle_timeout);
	++session.counter;
	return {*this, &session};
}

RealmSessionLease
SessionManager::Attach(RealmSessionLease lease, std::string_view realm,
		       std::span<const std::byte> attach) noexcept
{
	assert(attach.data() != nullptr);
	assert(!attach.empty());

	if (lease && lease->parent.IsAttach(attach))
		/* already set, no-op */
		return lease;

	if (lease && lease->parent.attach != nullptr) {
		sessions_by_attach.erase(sessions_by_attach.iterator_to(lease->parent));
		lease->parent.attach = nullptr;
	}

	const auto [it, inserted] =
		sessions_by_attach.insert_check(attach);
	if (inserted) {
		/* doesn't exist already */

		if (lease) {
			/* assign new "attach" value to the given session */
			lease->parent.attach = attach;
			sessions_by_attach.insert_commit(it, lease->parent);
			return lease;
		} else {
			/* create new session */

			auto l = CreateSession();
			l->attach = attach;
			sessions_by_attach.insert_commit(it, *l);

			return {std::move(l), realm};
		}
	} else {
		/* exists already */

		auto &existing = *it;

		if (lease) {
			auto &src = lease->parent;
			lease = nullptr;

			/* attach parameter session and to the
			   existing session */
			existing.Attach(std::move(src));

			EraseAndDispose(src);
		}

		return {SessionLease{*this, &existing}, realm};
	}
}

void
SessionManager::Put(Session &session) noexcept
{
	(void)session;
}

void
SessionManager::EraseAndDispose(SessionId id) noexcept
{
	auto i = sessions.find(id);
	if (i != sessions.end())
		EraseAndDispose(*i);
}

void
SessionManager::DiscardRealmSession(SessionId id, std::string_view realm_name) noexcept
{
	auto i = sessions.find(id);
	if (i == sessions.end())
		return;

	if (!i->DiscardRealm(realm_name))
		return;

	if (i->realms.empty())
		EraseAndDispose(*i);
}

void
SessionManager::Visit(void (*callback)(const Session *session,
				       void *ctx), void *ctx)
{
	const Expiry now = Expiry::Now();

	sessions.for_each([now, callback, ctx](const Session &session){
		if (!session.expires.IsExpired(now))
			callback(&session, ctx);
	});
}

void
SessionManager::DiscardAttachSession(std::span<const std::byte> attach) noexcept
{
	auto i = sessions_by_attach.find(attach);
	if (i != sessions_by_attach.end())
		EraseAndDispose(*i);
}
