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
#include "Session.hxx"
#include "io/Logger.hxx"
#include "util/DeleteDisposer.hxx"
#include "util/StaticArray.hxx"

#include <boost/intrusive/unordered_set.hpp>

#include <stdlib.h>

static constexpr unsigned MAX_SESSIONS = 65536;

struct SessionHash {
	gcc_pure
	size_t operator()(const SessionId &id) const {
		return id.Hash();
	}

	gcc_pure
	size_t operator()(const Session &session) const {
		return session.id.Hash();
	}
};

struct SessionEqual {
	gcc_pure
	bool operator()(const Session &a, const Session &b) const {
		return a.id == b.id;
	}

	gcc_pure
	bool operator()(const SessionId &a, const Session &b) const {
		return a == b.id;
	}
};

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

struct SessionContainer {
	/**
	 * The idle timeout of sessions [seconds].
	 */
	const std::chrono::seconds idle_timeout;

	using Set =
		boost::intrusive::unordered_set<Session,
						boost::intrusive::member_hook<Session,
									      Session::SetHook,
									      &Session::set_hook>,
						boost::intrusive::hash<SessionHash>,
						boost::intrusive::equal<SessionEqual>,
						boost::intrusive::constant_time_size<true>>;
	Set sessions;

	static constexpr unsigned N_BUCKETS = 16381;
	Set::bucket_type buckets[N_BUCKETS];

	explicit SessionContainer(std::chrono::seconds _idle_timeout)
		:idle_timeout(_idle_timeout),
		 sessions(Set::bucket_traits(buckets, N_BUCKETS)) {
	}

	~SessionContainer();

	unsigned Count() {
		return sessions.size();
	}

	Session *Find(SessionId id);

	void Put(Session &session) noexcept;

	void Insert(Session &session) {
		sessions.insert(session);
	}

	void EraseAndDispose(Session &session);
	void EraseAndDispose(SessionId id);

	void ReplaceAndDispose(Session &old_session, Session &new_session) {
		EraseAndDispose(old_session);
		Insert(new_session);
	}

	/**
	 * @return true if there is at least one session
	 */
	bool Cleanup() noexcept;

	/**
	 * Forcefully deletes at least one session.
	 */
	bool Purge() noexcept;

	bool Visit(bool (*callback)(const Session *session,
				    void *ctx), void *ctx);
};

#ifndef NDEBUG
/**
 * A process must not lock more than one session at a time, or it will
 * risk deadlocking itself.  For the assertions in this source, this
 * variable holds a reference to the locked session.
 */
static const Session *locked_session;
#endif

void
SessionContainer::EraseAndDispose(Session &session)
{
	assert(!sessions.empty());

	auto i = sessions.iterator_to(session);
	sessions.erase_and_dispose(i, DeleteDisposer{});
}

inline bool
SessionContainer::Cleanup() noexcept
{
	assert(locked_session == nullptr);

	const Expiry now = Expiry::Now();

	EraseAndDisposeIf(sessions, [now](const Session &session){
		return session.expires.IsExpired(now);
	}, DeleteDisposer{});

	return !sessions.empty();
}

SessionManager::SessionManager(EventLoop &event_loop,
			       std::chrono::seconds idle_timeout,
			       unsigned _cluster_size,
			       unsigned _cluster_node) noexcept
	:cluster_size(_cluster_size), cluster_node(_cluster_node),
	 container(new SessionContainer(idle_timeout)),
	 cleanup_timer(event_loop, BIND_THIS_METHOD(Cleanup)) {}

SessionManager::~SessionManager() noexcept
{
	delete container;
}

void
SessionManager::AdjustNewSessionId(SessionId &id) const noexcept
{
	if (cluster_size > 0)
		id.SetClusterNode(cluster_size, cluster_node);
}

unsigned
SessionManager::Count() noexcept
{
	assert(container != nullptr);

	return container->Count();
}

bool
SessionManager::Visit(bool (*callback)(const Session *session,
				       void *ctx), void *ctx)
{
	assert(container != nullptr);

	return container->Visit(callback, ctx);
}

Session *
SessionManager::Find(SessionId id) noexcept
{
	assert(container != nullptr);

	return container->Find(id);
}

void
SessionManager::Insert(Session &session) noexcept
{
	container->Insert(session);

	if (!cleanup_timer.IsPending())
		cleanup_timer.Schedule(cleanup_interval);
}

void
SessionManager::EraseAndDispose(SessionId id) noexcept
{
	assert(container != nullptr);

	container->EraseAndDispose(id);
}

void
SessionManager::ReplaceAndDispose(Session &old_session,
				  Session &new_session) noexcept
{
	container->ReplaceAndDispose(old_session, new_session);
}

bool
SessionManager::Purge() noexcept
{
	return container->Purge();
}

void
SessionManager::Cleanup() noexcept
{
	if (container->Cleanup())
		cleanup_timer.Schedule(cleanup_interval);
}

inline
SessionContainer::~SessionContainer()
{
	sessions.clear_and_dispose(DeleteDisposer{});
}

bool
SessionContainer::Purge() noexcept
{
	/* collect at most 256 sessions */
	StaticArray<Session *, 256> purge_sessions;
	unsigned highest_score = 0;

	assert(locked_session == nullptr);

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

Session *
SessionManager::CreateSession() noexcept
{
	assert(locked_session == nullptr);

	if (Count() >= MAX_SESSIONS)
		Purge();

	Session *session = new Session(GenerateSessionId());

#ifndef NDEBUG
	locked_session = session;
#endif

	Insert(*session);
	return session;
}

Session *
SessionContainer::Find(SessionId id)
{
	assert(locked_session == nullptr);

	auto i = sessions.find(id, SessionHash(), SessionEqual());
	if (i == sessions.end())
		return nullptr;

	Session &session = *i;

#ifndef NDEBUG
	locked_session = &session;
#endif

	session.expires.Touch(idle_timeout);
	++session.counter;
	return &session;
}

void
SessionContainer::Put(Session &session) noexcept
{
	assert(&session == locked_session);
	(void)session;

#ifndef NDEBUG
	locked_session = nullptr;
#endif
}

void
SessionManager::Put(Session &session) noexcept
{
	container->Put(session);
}

void
SessionContainer::EraseAndDispose(SessionId id)
{
	assert(locked_session == nullptr);

	Session *session = Find(id);
	if (session != nullptr) {
		Put(*session);
		EraseAndDispose(*session);
	}
}

inline bool
SessionContainer::Visit(bool (*callback)(const Session *session,
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
