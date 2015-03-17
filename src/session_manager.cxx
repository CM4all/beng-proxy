/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_manager.hxx"
#include "session.hxx"
#include "shm.h"
#include "dpool.h"
#include "rwlock.h"
#include "refcount.h"
#include "random.h"
#include "expiry.h"
#include "crash.h"
#include "clock.h"
#include "util/StaticArray.hxx"

#include <daemon/log.h>

#include <event.h>

#include <errno.h>
#include <string.h>
#include <stdlib.h>

#define SHM_PAGE_SIZE 4096
#define SHM_NUM_PAGES 32768
#define SM_PAGES ((sizeof(SessionManager) + SHM_PAGE_SIZE - 1) / SHM_PAGE_SIZE)

#define SESSION_SLOTS 16381

struct SessionManager {
    struct refcount ref;

    /**
     * The idle timeout of sessions [seconds].
     */
    const unsigned idle_timeout;

    const unsigned cluster_size, cluster_node;

    struct shm *const shm;

    /** this lock protects the following hash table */
    struct rwlock lock;

    /**
     * Has the session manager been abandoned after the crash of one
     * worker?  If this is true, then the session manager is disabled,
     * and the remaining workers will be shut down soon.
     */
    bool abandoned;

    typedef boost::intrusive::list<Session,
                                   boost::intrusive::member_hook<Session,
                                                                 Session::HashSiblingsHook,
                                                                 &Session::hash_siblings>,
                                   boost::intrusive::constant_time_size<false>> List;
    List sessions[SESSION_SLOTS];
    unsigned num_sessions;

    SessionManager(unsigned _idle_timeout,
                   unsigned _cluster_size, unsigned _cluster_node,
                   struct shm *_shm)
        :idle_timeout(_idle_timeout),
         cluster_size(_cluster_size),
         cluster_node(_cluster_node),
         shm(_shm),
         abandoned(false),
         num_sessions(0) {
        refcount_init(&ref);
        rwlock_init(&lock);
    }

    ~SessionManager();

    void Ref() {
        refcount_get(&ref);
        shm_ref(shm);
    }

    void Unref() {
        if (refcount_put(&ref))
            this->~SessionManager();
    }

    void Abandon();

    List &Slot(session_id_t id) {
#ifdef SESSION_ID_WORDS
        return sessions[id.data[0] % SESSION_SLOTS];
#else
        return sessions[id % SESSION_SLOTS];
#endif
    }

    void Insert(Session *session);

    void EraseAndDispose(Session *session);
    void EraseAndDispose(session_id_t id);

    bool Cleanup();

    /**
     * Forcefully deletes at least one session.
     */
    bool Purge();

    bool Visit(bool (*callback)(const Session *session,
                                void *ctx), void *ctx);
};

/** clean up expired sessions every 60 seconds */
static const struct timeval cleanup_interval = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static GRand *session_rand;

/** the one and only session manager instance, allocated from shared
    memory */
static SessionManager *session_manager;

/* this must be a separate variable, because session_manager is
   allocated from shared memory, and each process must manage its own
   event struct */
static struct event session_cleanup_event;

#ifndef NDEBUG
/**
 * A process must not lock more than one session at a time, or it will
 * risk deadlocking itself.  For the assertions in this source, this
 * variable holds a reference to the locked session.
 */
static const Session *locked_session;
#endif

void
SessionManager::EraseAndDispose(Session *session)
{
    assert(crash_in_unsafe());
    assert(rwlock_is_wlocked(&lock));
    assert(num_sessions > 0);

    auto &slot = Slot(session->id);
    slot.erase(slot.iterator_to(*session));
    --num_sessions;

    if (num_sessions == 0)
        evtimer_del(&session_cleanup_event);

    session_destroy(session);
}

inline bool
SessionManager::Cleanup()
{
    bool non_empty;

    assert(!crash_in_unsafe());
    assert(locked_session == nullptr);

    const unsigned now = now_s();

    crash_unsafe_enter();
    rwlock_wlock(&lock);

    if (abandoned) {
        rwlock_wunlock(&lock);
        crash_unsafe_leave();
        assert(!crash_in_unsafe());
        return false;
    }

    for (auto &slot : sessions) {
        slot.remove_and_dispose_if([now](const Session &session) {
                return now >= (unsigned)session.expires;
            },
            [this](Session *session) {
                assert(num_sessions > 0);

                --num_sessions;
                if (num_sessions == 0)
                    evtimer_del(&session_cleanup_event);

                session_destroy(session);
            });
    }

    non_empty = num_sessions > 0;

    rwlock_wunlock(&lock);
    crash_unsafe_leave();
    assert(!crash_in_unsafe());

    return non_empty;
}

static void
cleanup_event_callback(int fd gcc_unused, short event gcc_unused,
                       void *ctx gcc_unused)
{
    if (session_manager->Cleanup())
        evtimer_add(&session_cleanup_event, &cleanup_interval);
}

static SessionManager *
session_manager_new(unsigned idle_timeout,
                    unsigned cluster_size, unsigned cluster_node)
{
    struct shm *shm = shm_new(SHM_PAGE_SIZE, SHM_NUM_PAGES);
    if (shm == nullptr) {
        daemon_log(1, "shm_new() failed\n");
        abort();
    }

    return NewFromShm<SessionManager>(shm, SM_PAGES,
                                      idle_timeout,
                                      cluster_size, cluster_node,
                                      shm);
}

bool
session_manager_init(unsigned idle_timeout,
                     unsigned cluster_size, unsigned cluster_node)
{
    assert((cluster_size == 0 && cluster_node == 0) ||
           cluster_node < cluster_size);

    session_rand = g_rand_new();
    obtain_entropy(session_rand);

    if (session_manager == nullptr) {
        session_manager = session_manager_new(idle_timeout,
                                              cluster_size, cluster_node);
        if (session_manager == nullptr)
                return false;
    } else {
        session_manager->Ref();
    }

    evtimer_set(&session_cleanup_event, cleanup_event_callback, nullptr);

    return true;
}

inline
SessionManager::~SessionManager()
{
    crash_unsafe_enter();

    rwlock_wlock(&lock);

    for (auto &slot : sessions) {
        slot.clear_and_dispose([this](Session *session) {
                assert(num_sessions > 0);

                --num_sessions;
                if (num_sessions == 0)
                    evtimer_del(&session_cleanup_event);

                session_destroy(session);
            });
    }

    rwlock_wunlock(&lock);
    rwlock_destroy(&lock);

    crash_unsafe_leave();
}

void
session_manager_deinit()
{
    assert(session_manager != nullptr);
    assert(session_manager->shm != nullptr);
    assert(locked_session == nullptr);

    event_del(&session_cleanup_event);

    struct shm *shm = session_manager->shm;

    session_manager->Unref();
    session_manager = nullptr;

    /* we always destroy the SHM section, because it is not used
       anymore by this process; other processes may still use it */
    shm_close(shm);

    g_rand_free(session_rand);
}

inline void
SessionManager::Abandon()
{
    assert(shm != nullptr);

    abandoned = true;

    /* XXX move the "shm" pointer out of the shared memory */
    shm_close(shm);
}

void
session_manager_abandon()
{
    assert(session_manager != nullptr);

    event_del(&session_cleanup_event);

    session_manager->Abandon();
    session_manager = nullptr;
}

void
session_manager_event_add()
{
    if (session_manager->num_sessions == 0)
        evtimer_add(&session_cleanup_event, &cleanup_interval);
}

void
session_manager_event_del()
{
    event_del(&session_cleanup_event);
}

unsigned
session_manager_get_count()
{
    return session_manager->num_sessions;
}

struct dpool *
session_manager_new_dpool()
{
    return dpool_new(session_manager->shm);
}

bool
SessionManager::Purge()
{
    /* collect at most 256 sessions */
    StaticArray<Session *, 256> purge_sessions;
    unsigned highest_score = 0;

    assert(locked_session == nullptr);

    crash_unsafe_enter();
    rwlock_wlock(&lock);

    for (auto &slot : sessions) {
        for (auto &session : slot) {
            unsigned score = session_purge_score(&session);
            if (score > highest_score) {
                purge_sessions.clear();
                highest_score = score;
            }

            if (score == highest_score)
                purge_sessions.checked_append(&session);
        }
    }

    if (purge_sessions.empty()) {
        rwlock_wunlock(&lock);
        crash_unsafe_leave();
        return false;
    }

    daemon_log(3, "purging %u sessions (score=%u)\n",
               (unsigned)purge_sessions.size(), highest_score);

    for (auto session : purge_sessions) {
        lock_lock(&session->lock);
        EraseAndDispose(session);
    }

    rwlock_wunlock(&lock);
    crash_unsafe_leave();

    return true;
}

inline void
SessionManager::Insert(Session *session)
{
    assert(session != nullptr);

    rwlock_wlock(&lock);

    Slot(session->id).push_back(*session);
    ++num_sessions;

    const bool one_session = num_sessions == 1;

    rwlock_wunlock(&lock);

    if (one_session)
        evtimer_add(&session_cleanup_event, &cleanup_interval);
}

void
session_manager_add(Session *session)
{
    session_manager->Insert(session);
}

static uint32_t
cluster_session_id(uint32_t id)
{
    if (session_manager == nullptr || session_manager->cluster_size == 0)
        return id;

    uint32_t remainder = id % (uint32_t)session_manager->cluster_size;
    assert(remainder < session_manager->cluster_size);

    id -= remainder;
    id += session_manager->cluster_node;
    return id;
}

static void
session_generate_id(session_id_t *id_r)
{
#ifdef SESSION_ID_WORDS
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i)
        id_r->data[i] = g_rand_int(session_rand);

    id_r->data[SESSION_ID_WORDS - 1] =
        cluster_session_id(id_r->data[SESSION_ID_WORDS - 1]);
#else
    *id_r = (session_id_t)cluster_session_id(g_rand_int(session_rand))
        | (session_id_t)g_rand_int(session_rand) << 32;
#endif
}

static Session *
session_new_unsafe()
{
    struct dpool *pool;
    Session *session;
    unsigned num_sessions;

    assert(crash_in_unsafe());
    assert(locked_session == nullptr);

    if (session_manager->abandoned)
        return nullptr;

    pool = dpool_new(session_manager->shm);
    if (pool == nullptr) {
        if (!session_manager->Purge())
            return nullptr;

        /* at least one session has been purged: try again */
        pool = dpool_new(session_manager->shm);
        if (pool == nullptr)
            /* nope. fail. */
            return nullptr;
    }

    session = session_allocate(pool);
    if (session == nullptr) {
        dpool_destroy(pool);
        return nullptr;
    }

    session_generate_id(&session->id);

    rwlock_wlock(&session_manager->lock);

    session_manager->Slot(session->id).push_back(*session);
    ++session_manager->num_sessions;

    num_sessions = session_manager->num_sessions;

#ifndef NDEBUG
    locked_session = session;
#endif
    lock_lock(&session->lock);
    rwlock_wunlock(&session_manager->lock);

    if (num_sessions == 1)
        evtimer_add(&session_cleanup_event, &cleanup_interval);

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
static Session * gcc_malloc
session_defragment(Session *src)
{
    assert(crash_in_unsafe());

    struct dpool *pool;
    Session *dest;

    pool = dpool_new(session_manager->shm);
    if (pool == nullptr)
        return nullptr;

    dest = session_dup(pool, src);
    if (dest == nullptr) {
        dpool_destroy(pool);
        return src;
    }

    session_manager->Slot(dest->id).push_back(*dest);
    ++session_manager->num_sessions;

    session_manager->EraseAndDispose(src);
    return dest;
}

static Session *
session_find(session_id_t id)
{
    if (session_manager->abandoned)
        return nullptr;

    assert(crash_in_unsafe());
    assert(locked_session == nullptr);

    for (auto &session : session_manager->Slot(id)) {
        if (session_id_equals(session.id, id)) {
#ifndef NDEBUG
            locked_session = &session;
#endif
            lock_lock(&session.lock);

            session.expires = expiry_touch(session_manager->idle_timeout);
            ++session.counter;
            return &session;
        }
    }

    return nullptr;
}

Session *
session_get(session_id_t id)
{
    Session *session;

    assert(locked_session == nullptr);

    crash_unsafe_enter();
    rwlock_rlock(&session_manager->lock);
    session = session_find(id);
    rwlock_runlock(&session_manager->lock);

    if (session == nullptr)
        crash_unsafe_leave();

    return session;
}

static void
session_put_internal(Session *session)
{
    assert(crash_in_unsafe());
    assert(session == locked_session);

    lock_unlock(&session->lock);

#ifndef NDEBUG
    locked_session = nullptr;
#endif
}

static void
session_defragment_id(session_id_t id)
{
    assert(crash_in_unsafe());

    Session *session = session_find(id);
    if (session == nullptr)
        return;

    /* unlock the session, because session_defragment() may call
       SessionManager::EraseAndDispose(), and
       SessionManager::EraseAndDispose() expects the session to be
       unlocked.  This is ok, because we're holding the session
       manager lock at this point. */
    session_put_internal(session);

    session_defragment(session);
}

void
session_put(Session *session)
{
    session_id_t defragment;

    if ((session->counter % 1024) == 0 &&
        dpool_is_fragmented(session->pool))
        defragment = session->id;
    else
        session_id_clear(&defragment);

    session_put_internal(session);

    if (session_id_is_defined(defragment)) {
        /* the shared memory pool has become too fragmented;
           defragment the session by duplicating it into a new shared
           memory pool */

        rwlock_wlock(&session_manager->lock);
        session_defragment_id(defragment);
        rwlock_wunlock(&session_manager->lock);
    }

    crash_unsafe_leave();
}

void
SessionManager::EraseAndDispose(session_id_t id)
{
    Session *session;

    assert(locked_session == nullptr);

    crash_unsafe_enter();
    rwlock_wlock(&lock);

    session = session_find(id);
    if (session != nullptr) {
        session_put_internal(session);
        EraseAndDispose(session);
    }

    rwlock_wunlock(&lock);
    crash_unsafe_leave();
}

void
session_delete(session_id_t id)
{
    session_manager->EraseAndDispose(id);
}

inline bool
SessionManager::Visit(bool (*callback)(const Session *session,
                                       void *ctx), void *ctx)
{
    bool result = true;

    crash_unsafe_enter();
    rwlock_rlock(&lock);

    if (abandoned) {
        rwlock_runlock(&lock);
        crash_unsafe_leave();
        return false;
    }

    const unsigned now = now_s();

    for (auto &slot : sessions) {
        for (auto &session : slot) {
            if (now >= (unsigned)session.expires)
                continue;

            lock_lock(&session.lock);
            result = callback(&session, ctx);
            lock_unlock(&session.lock);

            if (!result)
                break;
        }

        if (!result)
               break;
    }

    rwlock_runlock(&lock);
    crash_unsafe_leave();

    return result;
}

bool
session_manager_visit(bool (*callback)(const Session *session,
                                       void *ctx), void *ctx)
{
    return session_manager->Visit(callback, ctx);
}
