/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_manager.hxx"
#include "session.hxx"
#include "shm/shm.hxx"
#include "shm/dpool.hxx"
#include "random.hxx"
#include "crash.hxx"
#include "system/clock.h"
#include "event/TimerEvent.hxx"
#include "util/StaticArray.hxx"
#include "util/RefCount.hxx"

#include <daemon/log.h>

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

struct SessionDisposer {
    void operator()(Session *session) {
        session_destroy(session);
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
    const unsigned idle_timeout;

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

    explicit SessionContainer(unsigned _idle_timeout)
        :idle_timeout(_idle_timeout),
         sessions(Set::bucket_traits(buckets, N_BUCKETS)) {
        ref.Init();
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
    bool Cleanup();

    /**
     * Forcefully deletes at least one session.
     */
    bool Purge();

    bool Visit(bool (*callback)(const Session *session,
                                void *ctx), void *ctx);
};

static constexpr size_t SHM_PAGE_SIZE = 4096;
static constexpr unsigned SHM_NUM_PAGES = 65536;
static constexpr unsigned SM_PAGES = (sizeof(SessionContainer) + SHM_PAGE_SIZE - 1) / SHM_PAGE_SIZE;

/** clean up expired sessions every 60 seconds */
static const struct timeval cleanup_interval = {
    .tv_sec = 60,
    .tv_usec = 0,
};

#ifndef NDEBUG
/**
 * A process must not lock more than one session at a time, or it will
 * risk deadlocking itself.  For the assertions in this source, this
 * variable holds a reference to the locked session.
 */
static const Session *locked_session;
#endif

class SessionManager {
    const unsigned cluster_size, cluster_node;

    struct shm *shm;

    SessionContainer *container;

    TimerEvent cleanup_timer;

public:
    SessionManager(EventLoop &event_loop, unsigned idle_timeout,
                   unsigned _cluster_size, unsigned _cluster_node)
        :cluster_size(_cluster_size), cluster_node(_cluster_node),
         shm(shm_new(SHM_PAGE_SIZE, SHM_NUM_PAGES)),
         container(NewFromShm<SessionContainer>(shm, SM_PAGES, idle_timeout)),
         cleanup_timer(event_loop, BIND_THIS_METHOD(Cleanup)) {}

    ~SessionManager() {
        cleanup_timer.Cancel();

        if (container != nullptr)
            container->Unref();

        if (shm != nullptr)
            shm_close(shm);
    }

    void EnableEvents() {
        cleanup_timer.Add(cleanup_interval);
    }

    void DisableEvents() {
        cleanup_timer.Cancel();
    }

    void Ref() {
        assert(container != nullptr);
        assert(shm != nullptr);

        container->Ref();
        shm_ref(shm);
    }

    void Abandon() {
        assert(container != nullptr);
        assert(shm != nullptr);

        container->Abandon();
    }

    bool IsAbandoned() const {
        return container == nullptr || container->abandoned;
    }

    void AdjustNewSessionId(SessionId &id) {
        if (cluster_size > 0)
            id.SetClusterNode(cluster_size, cluster_node);
    }

    unsigned Count() {
        assert(container != nullptr);

        return container->Count();
    }

    unsigned LockCount() {
        assert(container != nullptr);

        return container->LockCount();
    }

    bool Visit(bool (*callback)(const Session *session,
                                void *ctx), void *ctx) {
        assert(container != nullptr);

        return container->Visit(callback, ctx);
    }

    Session *Find(SessionId id) {
        assert(container != nullptr);

        return container->LockFind(id);
    }

    void Insert(Session &session) {
        container->LockInsert(session);

        if (!cleanup_timer.IsPending())
            cleanup_timer.Add(cleanup_interval);
    }

    void EraseAndDispose(SessionId id) {
        assert(container != nullptr);

        container->LockEraseAndDispose(id);
    }

    void ReplaceAndDispose(Session &old_session, Session &new_session) {
        container->ReplaceAndDispose(old_session, new_session);
    }

    void Defragment(SessionId id) {
        assert(container != nullptr);
        assert(shm != nullptr);

        container->LockDefragment(id, *shm);
    }

    bool Purge() {
        return container->Purge();
    }

    void Cleanup();

    struct dpool *NewDpool() {
        return dpool_new(*shm);
    }

    struct dpool *NewDpoolHarder() {
        auto *pool = NewDpool();
        if (pool == nullptr && Purge())
            /* at least one session has been purged: try again */
            pool = NewDpool();

        return pool;
    }
};

/** the one and only session manager instance */
static SessionManager *session_manager;

void
SessionContainer::EraseAndDispose(Session &session)
{
    assert(crash_in_unsafe());
    assert(!sessions.empty());

    auto i = sessions.iterator_to(session);
    sessions.erase_and_dispose(i, SessionDisposer());
}

inline bool
SessionContainer::Cleanup()
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
        }, SessionDisposer());

    return !sessions.empty();
}

void
SessionManager::Cleanup()
{
    if (container->Cleanup())
        cleanup_timer.Add(cleanup_interval);

    assert(!crash_in_unsafe());
}

void
session_manager_init(EventLoop &event_loop, unsigned idle_timeout,
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

    sessions.clear_and_dispose(SessionDisposer());
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

void
session_manager_event_add()
{
    session_manager->EnableEvents();
}

void
session_manager_event_del()
{
    session_manager->DisableEvents();
}

unsigned
session_manager_get_count()
{
    return session_manager->LockCount();
}

struct dpool *
session_manager_new_dpool()
{
    return session_manager->NewDpool();
}

bool
SessionContainer::Purge()
{
    /* collect at most 256 sessions */
    StaticArray<Session *, 256> purge_sessions;
    unsigned highest_score = 0;

    assert(locked_session == nullptr);

    const ScopeCrashUnsafe crash_unsafe;
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);

    for (auto &session : sessions) {
        unsigned score = session_purge_score(&session);
        if (score > highest_score) {
            purge_sessions.clear();
            highest_score = score;
        }

        if (score == highest_score)
            purge_sessions.checked_append(&session);
    }

    if (purge_sessions.empty())
        return false;

    daemon_log(3, "purging %u sessions (score=%u)\n",
               (unsigned)purge_sessions.size(), highest_score);

    for (auto session : purge_sessions) {
        session->mutex.lock();
        EraseAndDispose(*session);
    }

    /* purge again if the highest score group has only very few items,
       which would lead to calling this (very expensive) function too
       often */
    bool again = purge_sessions.size() < 16 &&
        session_manager->Count() > SHM_NUM_PAGES - 256;

    lock.unlock();

    if (again)
        Purge();

    return true;
}

void
session_manager_add(Session &session)
{
    session_manager->Insert(session);
}

static void
session_generate_id(SessionId *id_r)
{
    id_r->Generate();

    if (session_manager != nullptr)
        session_manager->AdjustNewSessionId(*id_r);
}

static Session *
session_new_unsafe(const char *realm)
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
        session = session_allocate(pool, realm);
    } catch (std::bad_alloc) {
        dpool_destroy(pool);
        return nullptr;
    }

    session_generate_id(&session->id);

#ifndef NDEBUG
    locked_session = session;
#endif
    session->mutex.lock();

    session_manager->Insert(*session);

    return session;
}

Session *
session_new(const char *realm)
{
    crash_unsafe_enter();
    Session *session = session_new_unsafe(realm);
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
        dest = session_dup(pool, &src);
    } catch (std::bad_alloc) {
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

    crash_unsafe_enter();

    Session *session = session_manager->Find(id);

    if (session == nullptr)
        crash_unsafe_leave();

    return session;
}

static void
session_put_internal(Session *session)
{
    assert(crash_in_unsafe());
    assert(session == locked_session);

    session->mutex.unlock();

#ifndef NDEBUG
    locked_session = nullptr;
#endif
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
    session_put_internal(session);

    Defragment(*session, shm);
}

void
session_put(Session *session)
{
    SessionId defragment;

    if ((session->counter % 1024) == 0 &&
        dpool_is_fragmented(session->pool))
        defragment = session->id;
    else
        defragment.Clear();

    session_put_internal(session);

    if (defragment.IsDefined())
        /* the shared memory pool has become too fragmented;
           defragment the session by duplicating it into a new shared
           memory pool */
        session_manager->Defragment(defragment);

    crash_unsafe_leave();
}

void
SessionContainer::LockEraseAndDispose(SessionId id)
{
    assert(locked_session == nullptr);

    const ScopeCrashUnsafe crash_unsafe;
    boost::interprocess::scoped_lock<boost::interprocess::interprocess_sharable_mutex> lock(mutex);

    Session *session = session_manager->Find(id);
    if (session != nullptr) {
        session_put_internal(session);
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

bool
session_manager_visit(bool (*callback)(const Session *session,
                                       void *ctx), void *ctx)
{
    return session_manager->Visit(callback, ctx);
}
