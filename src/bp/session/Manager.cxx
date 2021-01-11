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

#include "Manager.hxx"
#include "Lease.hxx"
#include "io/Logger.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/StaticArray.hxx"
#include "util/djbhash.h"

#include <cassert>

static constexpr unsigned MAX_SESSIONS = 65536;

inline size_t
SessionManager::SessionAttachHash::operator()(ConstBuffer<std::byte> attach) const noexcept
{
	return djb_hash(attach.data, attach.size);
}

inline size_t
SessionManager::SessionAttachHash::operator()(const Session &session) const noexcept
{
	return session.id.Hash();
}

template<typename Container, typename Pred, typename Disposer>
static void
EraseAndDisposeIf(Container &container, Pred pred, Disposer disposer)
{
	for (auto i = container.begin(), end = container.end(); i != end;) {
		const auto next = std::next(i);

		if (pred(*i))
			container.erase_and_dispose(i, disposer);

		i = next;
	}
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

	EraseAndDisposeIf(sessions, [now](const Session &session){
		return session.expires.IsExpired(now);
	}, DeleteDisposer{});

	if (!sessions.empty())
		cleanup_timer.Schedule(cleanup_interval);
}

SessionManager::SessionManager(EventLoop &event_loop,
			       std::chrono::seconds _idle_timeout,
			       unsigned _cluster_size,
			       unsigned _cluster_node) noexcept
	:cluster_size(_cluster_size), cluster_node(_cluster_node),
	 idle_timeout(_idle_timeout),
	 sessions(Set::bucket_traits(buckets, N_BUCKETS)),
	 sessions_by_attach(ByAttach::bucket_traits(buckets_by_attach, N_BUCKETS)),
	 cleanup_timer(event_loop, BIND_THIS_METHOD(Cleanup))
{
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
	StaticArray<Session *, 256> purge_sessions;
	unsigned highest_score = 0;

	for (auto &session : sessions) {
		unsigned score = session.GetPurgeScore();
		if (score > highest_score) {
			purge_sessions.clear();
			highest_score = score;
		}

		if (score == highest_score)
			purge_sessions.checked_append(&session);
	}

	if (purge_sessions.empty())
		return false;

	LogConcat(3, "SessionManager", "purging ", (unsigned)purge_sessions.size(),
		  " sessions (score=", highest_score, ")");

	for (auto session : purge_sessions) {
		EraseAndDispose(*session);
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
SessionManager::GenerateSessionId() const noexcept
{
	SessionId id;
	id.Generate();
	AdjustNewSessionId(id);
	return id;
}

SessionLease
SessionManager::CreateSession() noexcept
{
	if (Count() >= MAX_SESSIONS)
		Purge();

	Session *session = new Session(GenerateSessionId());
	Insert(*session);
	return {*this, session};
}

SessionLease
SessionManager::Find(SessionId id) noexcept
{
	auto i = sessions.find(id, SessionHash(), SessionEqual());
	if (i == sessions.end())
		return nullptr;

	Session &session = *i;

	session.expires.Touch(idle_timeout);
	++session.counter;
	return {*this, &session};
}

RealmSessionLease
SessionManager::Attach(RealmSessionLease lease, const char *realm,
		       ConstBuffer<std::byte> attach) noexcept
{
	assert(attach != nullptr);
	assert(!attach.empty());

	if (lease && sessions_by_attach.key_eq()(attach, lease->parent))
		/* already set, no-op */
		return lease;

	if (lease && lease->parent.attach != nullptr) {
		sessions_by_attach.erase(sessions_by_attach.iterator_to(lease->parent));
		lease->parent.attach = nullptr;
	}

	ByAttach::insert_commit_data commit_data;
	const auto [it, inserted] =
		sessions_by_attach.insert_check(attach,
						sessions_by_attach.hash_function(),
						sessions_by_attach.key_eq(),
						commit_data);
	if (inserted) {
		/* doesn't exist already */

		if (lease) {
			/* assign new "attach" value to the given session */
			lease->parent.attach = attach;
			sessions_by_attach.insert_commit(lease->parent,
							 commit_data);
			return lease;
		} else {
			/* create new session */

			auto l = CreateSession();
			l->attach = attach;
			sessions_by_attach.insert_commit(*l, commit_data);

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
	auto i = sessions.find(id, SessionHash(), SessionEqual());
	if (i != sessions.end())
		EraseAndDispose(*i);
}

bool
SessionManager::Visit(bool (*callback)(const Session *session,
				       void *ctx), void *ctx)
{
	const Expiry now = Expiry::Now();

	for (auto &session : sessions) {
		if (session.expires.IsExpired(now))
			continue;

		{
			if (!callback(&session, ctx))
				return false;
		}
	}

	return true;
}
