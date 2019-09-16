/*
 * Copyright 2007-2018 Content Management AG
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
#include "shm/shm.hxx"
#include "shm/dpool.hxx"
#include "random.hxx"
#include "crash.hxx"
#include "io/Logger.hxx"
#include "util/StaticArray.hxx"
#include "util/RefCount.hxx"

#include <boost/interprocess/sync/interprocess_sharable_mutex.hpp>
#include <boost/interprocess/sync/sharable_lock.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>

#include <errno.h>
#include <string.h>
#include <stdlib.h>

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
    RefCount ref;

    /**
     * The idle timeout of sessions [seconds].
     */
    const std::chrono::seconds idle_timeout;

    /** this lock protects the following hash table */
    boost::interprocess::interprocess_sharable_mutex mutex;

    /**
     * Has the session manager been abandoned after the crash of one
     * worker?  If this is true, then the session manager is disabled,
     * and the remaining workers will be shut down soon.
     */
    bool abandoned = false;

    typedef boost::intrusive::unordered_set<Session,
                                            boost::intrusive::member_hook<Session,
                                                                          Session::SetHook,
                                                                          &Session::set_hook>,
                                            boost::intrusive::hash<SessionHash>,
                                            boost::intrusive::equal<SessionEqual>,
                                            boost::intrusive::constant_time_size<true>> Set;
    Set sessions;

    static constexpr unsigned N_BUCKETS = 16381;
    Set::bucket_type buckets[N_BUCKETS];

    explicit SessionContainer(std::chrono::seconds _idle_timeout)
        :idle_timeout(_idle_timeout),
         sessions(Set::bucket_traits(buckets, N_BUCKETS)) {
    }

    ~SessionContainer();

    void Ref() {
        ref.Get();
    }

    void Unref() {
        if (ref.Put())
            this->~SessionContainer();
    }

    void Abandon() {
        abandoned = true;
    }

    bool IsAbandoned() const {
        return abandoned;
    }

    unsigned Count() {
        return sessions.size();
    }

    unsigned LockCount() {
        boost::interprocess::sharable_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);
        return sessions.size();
    }

    Session *Find(SessionId id);

    Session *LockFind(SessionId id) {
        boost::interprocess::sharable_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);
        return Find(id);
    }

    void Put(Session &session) noexcept;

    void Insert(Session &session) {
        sessions.insert(session);
    }

    void LockInsert(Session &session) {
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);
        Insert(session);
    }

    void EraseAndDispose(Session &session);
    void LockEraseAndDispose(SessionId id);

    void ReplaceAndDispose(Session &old_session, Session &new_session) {
        EraseAndDispose(old_session);
        Insert(new_session);
    }

    void Defragment(Session &src, struct shm &shm);
    void Defragment(SessionId id, struct shm &shm);

    void LockDefragment(SessionId id, struct shm &shm) {
        boost::interprocess::scoped_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);
        Defragment(id, shm);
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

static constexpr size_t SHM_PAGE_SIZE = 4096;
static constexpr unsigned SHM_NUM_PAGES = 65536;
static constexpr unsigned SM_PAGES = (sizeof(SessionContainer) + SHM_PAGE_SIZE - 1) / SHM_PAGE_SIZE;

#ifndef NDEBUG
/**
 * A process must not lock more than one session at a time, or it will
 * risk deadlocking itself.  For the assertions in this source, this
 * variable holds a reference to the locked session.
 */
static const Session *locked_session;
#endif

SessionManager *session_manager;

void
SessionContainer::EraseAndDispose(Session &session)
{
    assert(crash_in_unsafe());
    assert(!sessions.empty());

    auto i = sessions.iterator_to(session);
    sessions.erase_and_dispose(i, Session::Disposer());
}

inline bool
SessionContainer::Cleanup() noexcept
{
    assert(!crash_in_unsafe());
    assert(locked_session == nullptr);

    const Expiry now = Expiry::Now();

    const ScopeCrashUnsafe crash_unsafe;
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);

    if (abandoned) {
        assert(!crash_in_unsafe());
        return false;
    }

    EraseAndDisposeIf(sessions, [now](const Session &session){
            return session.expires.IsExpired(now);
        }, Session::Disposer());

    return !sessions.empty();
}

SessionManager::SessionManager(EventLoop &event_loop,
                               std::chrono::seconds idle_timeout,
                               unsigned _cluster_size,
                               unsigned _cluster_node) noexcept
    :cluster_size(_cluster_size), cluster_node(_cluster_node),
     shm(shm_new(SHM_PAGE_SIZE, SHM_NUM_PAGES)),
     container(NewFromShm<SessionContainer>(shm, SM_PAGES, idle_timeout)),
     cleanup_timer(event_loop, BIND_THIS_METHOD(Cleanup)) {}

SessionManager::~SessionManager() noexcept
{
    if (container != nullptr)
        container->Unref();

    if (shm != nullptr)
        shm_close(shm);
}

void
SessionManager::Ref() noexcept
{
    assert(container != nullptr);
    assert(shm != nullptr);

    container->Ref();
    shm_ref(shm);
}

void
SessionManager::Abandon() noexcept
{
    assert(container != nullptr);
    assert(shm != nullptr);

    container->Abandon();
}

bool
SessionManager::IsAbandoned() const noexcept
{
    return container == nullptr || container->abandoned;
}

void
SessionManager::AdjustNewSessionId(SessionId &id) noexcept
{
    if (cluster_size > 0)
        id.SetClusterNode(cluster_size, cluster_node);
}

unsigned
SessionManager::LockCount() noexcept
{
    assert(container != nullptr);

    return container->LockCount();
}

bool
SessionManager::Visit(bool (*callback)(const Session *session,
                                       void *ctx), void *ctx)
{
    assert(container != nullptr);

    return container->Visit(callback, ctx);
}

Session *
SessionManager::LockFind(SessionId id) noexcept
{
    assert(container != nullptr);

    return container->LockFind(id);
}

void
SessionManager::Insert(Session &session) noexcept
{
    container->LockInsert(session);

    if (!cleanup_timer.IsPending())
        cleanup_timer.Schedule(cleanup_interval);
}

void
SessionManager::EraseAndDispose(SessionId id) noexcept
{
    assert(container != nullptr);

    container->LockEraseAndDispose(id);
}

void
SessionManager::ReplaceAndDispose(Session &old_session,
                                  Session &new_session) noexcept
{
    container->ReplaceAndDispose(old_session, new_session);
}

void
SessionManager::Defragment(SessionId id) noexcept
{
    assert(container != nullptr);
    assert(shm != nullptr);

    container->LockDefragment(id, *shm);
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

    assert(!crash_in_unsafe());
}

struct dpool *
SessionManager::NewDpool() noexcept
{
    return dpool_new(*shm);
}

void
session_manager_init(EventLoop &event_loop, std::chrono::seconds idle_timeout,
                     unsigned cluster_size, unsigned cluster_node)
{
    assert((cluster_size == 0 && cluster_node == 0) ||
           cluster_node < cluster_size);

    random_seed();

    if (session_manager == nullptr) {
        session_manager = new SessionManager(event_loop, idle_timeout,
                                             cluster_size, cluster_node);
    } else {
        session_manager->Ref();
    }
}

inline
SessionContainer::~SessionContainer()
{
    const ScopeCrashUnsafe crash_unsafe;
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);

    sessions.clear_and_dispose(Session::Disposer());
}

void
session_manager_deinit()
{
    assert(session_manager != nullptr);
    assert(locked_session == nullptr);

    delete session_manager;
    session_manager = nullptr;
}

void
session_manager_abandon()
{
    assert(session_manager != nullptr);

    session_manager->Abandon();
    delete session_manager;
    session_manager = nullptr;
}

bool
SessionContainer::Purge() noexcept
{
    /* collect at most 256 sessions */
    StaticArray<Session *, 256> purge_sessions;
    unsigned highest_score = 0;

    assert(locked_session == nullptr);

    const ScopeCrashUnsafe crash_unsafe;
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);

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
        /* lock the session to make sure it's not currently in use by
           another worker (will wait for the other worker) */
        session->mutex.lock();
        /* release the mutex right after that to avoid assertion
           failure in the mutex destructor; meanwhile, no other worker
           can get a reference to this unlocked session, because the
           SessionContainer is locked */
        session->mutex.unlock();

        EraseAndDispose(*session);
    }

    /* purge again if the highest score group has only very few items,
       which would lead to calling this (very expensive) function too
       often */
    bool again = purge_sessions.size() < 16 &&
        Count() > SHM_NUM_PAGES - 256;

    lock.unlock();

    if (again)
        Purge();

    return true;
}

static SessionId
GenerateSessionId()
{
    SessionId id;
    id.Generate();

    if (session_manager != nullptr)
        session_manager->AdjustNewSessionId(id);

    return id;
}

static Session *
session_new_unsafe()
{
    assert(crash_in_unsafe());
    assert(locked_session == nullptr);

    if (session_manager->IsAbandoned())
        return nullptr;

    struct dpool *pool = session_manager->NewDpoolHarder();
    if (pool == nullptr)
        return nullptr;

    Session *session;

    try {
        session = NewFromPool<Session>(*pool, *pool, GenerateSessionId());
    } catch (const std::bad_alloc &) {
        dpool_destroy(pool);
        return nullptr;
    }

#ifndef NDEBUG
    locked_session = session;
#endif
    session->mutex.lock();

    session_manager->Insert(*session);

    return session;
}

Session *
session_new()
{
    crash_unsafe_enter();
    Session *session = session_new_unsafe();
    if (session == nullptr)
        crash_unsafe_leave();
    return session;
}

/**
 * After a while the dpool may have fragmentations, and memory is
 * wasted.  This function duplicates the session into a fresh dpool,
 * and frees the old session instance.  Of course, this requires that
 * there is enough free shared memory.
 */
void
SessionContainer::Defragment(Session &src, struct shm &shm)
{
    assert(crash_in_unsafe());

    struct dpool *pool = dpool_new(shm);
    if (pool == nullptr)
        return;

    Session *dest;
    try {
        dest = NewFromPool<Session>(*pool, *pool, src);
    } catch (const std::bad_alloc &) {
        dpool_destroy(pool);
        return;
    }

    ReplaceAndDispose(src, *dest);
}

Session *
SessionContainer::Find(SessionId id)
{
    if (abandoned)
        return nullptr;

    assert(crash_in_unsafe());
    assert(locked_session == nullptr);

    auto i = sessions.find(id, SessionHash(), SessionEqual());
    if (i == sessions.end())
        return nullptr;

    Session &session = *i;

#ifndef NDEBUG
    locked_session = &session;
#endif
    session.mutex.lock();

    session.expires.Touch(idle_timeout);
    ++session.counter;
    return &session;
}

Session *
session_get(SessionId id)
{
    assert(locked_session == nullptr);

    if (!id.IsDefined())
        return nullptr;

    crash_unsafe_enter();

    Session *session = session_manager->LockFind(id);

    if (session == nullptr)
        crash_unsafe_leave();

    return session;
}

void
SessionContainer::Put(Session &session) noexcept
{
    assert(crash_in_unsafe());
    assert(&session == locked_session);

    session.mutex.unlock();

#ifndef NDEBUG
    locked_session = nullptr;
#endif
}

void
SessionManager::Put(Session &session) noexcept
{
    assert(crash_in_unsafe());

    SessionId defragment;

    if ((session.counter % 1024) == 0 &&
        dpool_is_fragmented(session.pool))
        defragment = session.id;
    else
        defragment.Clear();

    container->Put(session);

    if (defragment.IsDefined())
        /* the shared memory pool has become too fragmented;
           defragment the session by duplicating it into a new shared
           memory pool */
        Defragment(defragment);
}

void
SessionContainer::Defragment(SessionId id, struct shm &shm)
{
    assert(crash_in_unsafe());

    Session *session = Find(id);
    if (session == nullptr)
        return;

    /* unlock the session, because session_defragment() may call
       SessionContainer::EraseAndDispose(), and
       SessionContainer::EraseAndDispose() expects the session to be
       unlocked.  This is ok, because we're holding the session
       manager lock at this point. */
    Put(*session);

    Defragment(*session, shm);
}

void
session_put(Session *session)
{
    session_manager->Put(*session);
    crash_unsafe_leave();
}

void
SessionContainer::LockEraseAndDispose(SessionId id)
{
    assert(locked_session == nullptr);

    const ScopeCrashUnsafe crash_unsafe;
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);

    Session *session = Find(id);
    if (session != nullptr) {
        Put(*session);
        EraseAndDispose(*session);
    }
}

void
session_delete(SessionId id)
{
    session_manager->EraseAndDispose(id);
}

inline bool
SessionContainer::Visit(bool (*callback)(const Session *session,
                                         void *ctx), void *ctx)
{
    const ScopeCrashUnsafe crash_unsafe;
    boost::interprocess::sharable_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);

    if (abandoned) {
        return false;
    }

    const Expiry now = Expiry::Now();

    for (auto &session : sessions) {
        if (session.expires.IsExpired(now))
            continue;

        {
            boost::interprocess::scoped_lock<boost::interprocess::interprocess_mutex> scoped_lock(session.mutex);
            if (!callback(&session, ctx))
                return false;
        }
    }

    return true;
}
