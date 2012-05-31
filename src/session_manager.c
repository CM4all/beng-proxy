/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session_manager.h"
#include "session.h"
#include "shm.h"
#include "dpool.h"
#include "rwlock.h"
#include "refcount.h"
#include "random.h"
#include "expiry.h"
#include "crash.h"

#include <daemon/log.h>

#include <event.h>

#include <errno.h>
#include <stdlib.h>

#define SHM_PAGE_SIZE 4096
#define SHM_NUM_PAGES 32768
#define SM_PAGES ((sizeof(struct session_manager) + SHM_PAGE_SIZE - 1) / SHM_PAGE_SIZE)

#define SESSION_SLOTS 16381

struct session_manager {
    struct refcount ref;

    /**
     * The idle timeout of sessions [seconds].
     */
    unsigned idle_timeout;

    unsigned cluster_size, cluster_node;

    struct shm *shm;

    /** this lock protects the following hash table */
    struct rwlock lock;

    /**
     * Has the session manager been abandoned after the crash of one
     * worker?  If this is true, then the session manager is disabled,
     * and the remaining workers will be shut down soon.
     */
    bool abandoned;

    struct list_head sessions[SESSION_SLOTS];
    unsigned num_sessions;
};

/** clean up expired sessions every 60 seconds */
static const struct timeval cleanup_interval = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static GRand *session_rand;

/** the one and only session manager instance, allocated from shared
    memory */
static struct session_manager *session_manager;

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
static const struct session *locked_session;
#endif

static void
session_remove(struct session *session)
{
    assert(crash_in_unsafe());
    assert(rwlock_is_wlocked(&session_manager->lock));
    assert(session_manager->num_sessions > 0);

    list_remove(&session->hash_siblings);
    --session_manager->num_sessions;

    if (session_manager->num_sessions == 0)
        evtimer_del(&session_cleanup_event);

    session_destroy(session);
}

static void
cleanup_event_callback(int fd gcc_unused, short event gcc_unused,
                       void *ctx gcc_unused)
{
    struct timespec now;
    int ret;
    unsigned i;
    struct session *session, *next;
    bool non_empty;

    assert(!crash_in_unsafe());
    assert(locked_session == NULL);

    ret = clock_gettime(CLOCK_MONOTONIC, &now);
    if (ret < 0) {
        daemon_log(1, "clock_gettime(CLOCK_MONOTONIC) failed: %s\n",
                   strerror(errno));
        return;
    }

    crash_unsafe_enter();
    rwlock_wlock(&session_manager->lock);

    if (session_manager->abandoned) {
        rwlock_wunlock(&session_manager->lock);
        crash_unsafe_leave();
        assert(!crash_in_unsafe());
        return;
    }

    for (i = 0; i < SESSION_SLOTS; ++i) {
        for (session = (struct session *)session_manager->sessions[i].next;
             &session->hash_siblings != &session_manager->sessions[i];
             session = next) {
            next = (struct session *)session->hash_siblings.next;
            if (now.tv_sec >= session->expires)
                session_remove(session);
        }
    }

    non_empty = session_manager->num_sessions > 0;

    rwlock_wunlock(&session_manager->lock);
    crash_unsafe_leave();
    assert(!crash_in_unsafe());

    if (non_empty) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_cleanup_event, &tv);
    }
}

static struct session_manager *
session_manager_new(unsigned idle_timeout,
                    unsigned cluster_size, unsigned cluster_node)
{
    struct shm *shm;
    struct session_manager *sm;
    unsigned i;

    shm = shm_new(SHM_PAGE_SIZE, SHM_NUM_PAGES);
    if (shm == NULL) {
        daemon_log(1, "shm_new() failed\n");
        abort();
    }

    sm = shm_alloc(shm, SM_PAGES);
    refcount_init(&sm->ref);
    sm->idle_timeout = idle_timeout;
    sm->cluster_size = cluster_size;
    sm->cluster_node = cluster_node;
    sm->shm = shm;

    rwlock_init(&sm->lock);

    sm->abandoned = false;

    for (i = 0; i < SESSION_SLOTS; ++i)
        list_init(&sm->sessions[i]);

    sm->num_sessions = 0;

    return sm;
}

bool
session_manager_init(unsigned idle_timeout,
                     unsigned cluster_size, unsigned cluster_node)
{
    assert((cluster_size == 0 && cluster_node == 0) ||
           cluster_node < cluster_size);

    session_rand = g_rand_new();
    obtain_entropy(session_rand);

    if (session_manager == NULL) {
        session_manager = session_manager_new(idle_timeout,
                                              cluster_size, cluster_node);
        if (session_manager == NULL)
                return false;
    } else {
        refcount_get(&session_manager->ref);
        shm_ref(session_manager->shm);
    }

    evtimer_set(&session_cleanup_event, cleanup_event_callback, NULL);

    return true;
}

static void
session_manager_destroy(struct session_manager *sm)
{
    unsigned i;

    assert(locked_session == NULL);

    crash_unsafe_enter();
    rwlock_wlock(&sm->lock);

    for (i = 0; i < SESSION_SLOTS; ++i) {
        while (!list_empty(&sm->sessions[i])) {
            struct session *session = (struct session *)sm->sessions[i].next;
            session_remove(session);
        }
    }

    rwlock_wunlock(&sm->lock);
    rwlock_destroy(&sm->lock);
    crash_unsafe_leave();

    g_rand_free(session_rand);
}

void
session_manager_deinit(void)
{
    assert(session_manager != NULL);
    assert(session_manager->shm != NULL);
    assert(locked_session == NULL);

    event_del(&session_cleanup_event);

    if (refcount_put(&session_manager->ref))
        session_manager_destroy(session_manager);

    /* we always destroy the SHM section, because it is not used
       anymore by this process; other processes may still use it */
    shm_close(session_manager->shm);

    session_manager = NULL;
}

void
session_manager_abandon(void)
{
    assert(session_manager != NULL);
    assert(session_manager->shm != NULL);

    session_manager->abandoned = true;

    event_del(&session_cleanup_event);

    /* XXX move the "shm" pointer out of the shared memory */
    shm_close(session_manager->shm);
    session_manager = NULL;
}

void
session_manager_event_add(void)
{
    if (session_manager->num_sessions == 0) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_cleanup_event, &tv);
    }
}

void
session_manager_event_del(void)
{
    event_del(&session_cleanup_event);
}

struct dpool *
session_manager_new_dpool(void)
{
    return dpool_new(session_manager->shm);
}

/**
 * Forcefully deletes at least one session.
 */
static bool
session_manager_purge(void)
{
    /* collect at most 256 sessions */
    struct session *sessions[256];
    unsigned num_sessions = 0, highest_score = 0;

    assert(locked_session == NULL);

    crash_unsafe_enter();
    rwlock_wlock(&session_manager->lock);

    for (unsigned i = 0; i < SESSION_SLOTS; ++i) {
        for (struct session *s = (struct session *)session_manager->sessions[i].next;
             &s->hash_siblings != &session_manager->sessions[i];
             s = (struct session *)s->hash_siblings.next) {
            unsigned score = session_purge_score(s);
            if (score > highest_score) {
                num_sessions = 0;
                highest_score = score;
            }

            if (score == highest_score && num_sessions < 256)
                sessions[num_sessions++] = s;
        }
    }

    if (num_sessions == 0) {
        rwlock_wunlock(&session_manager->lock);
        crash_unsafe_leave();
        return false;
    }

    daemon_log(3, "purging %u sessions (score=%u)\n",
               num_sessions, highest_score);

    for (unsigned i = 0; i < num_sessions; ++i) {
        lock_lock(&sessions[i]->lock);
        session_remove(sessions[i]);
    }

    rwlock_wunlock(&session_manager->lock);
    crash_unsafe_leave();

    return true;
}

static struct list_head *
session_slot(session_id_t id)
{
#ifdef SESSION_ID_WORDS
    return &session_manager->sessions[id.data[0] % SESSION_SLOTS];
#else
    return &session_manager->sessions[id % SESSION_SLOTS];
#endif
}

void
session_manager_add(struct session *session)
{
    assert(session != NULL);

    rwlock_wlock(&session_manager->lock);

    list_add(&session->hash_siblings, session_slot(session->id));
    ++session_manager->num_sessions;

    const unsigned num_sessions = session_manager->num_sessions;

    rwlock_wunlock(&session_manager->lock);

    if (num_sessions == 1) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_cleanup_event, &tv);
    }
}

static uint32_t
cluster_session_id(uint32_t id)
{
    if (session_manager == NULL || session_manager->cluster_size == 0)
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

static struct session *
session_new_unsafe(void)
{
    struct dpool *pool;
    struct session *session;
    unsigned num_sessions;

    assert(crash_in_unsafe());
    assert(locked_session == NULL);

    if (session_manager->abandoned)
        return NULL;

    pool = dpool_new(session_manager->shm);
    if (pool == NULL) {
        if (!session_manager_purge())
            return NULL;

        /* at least one session has been purged: try again */
        pool = dpool_new(session_manager->shm);
        if (pool == NULL)
            /* nope. fail. */
            return NULL;
    }

    session = session_allocate(pool);
    if (session == NULL) {
        dpool_destroy(pool);
        return NULL;
    }

    session_generate_id(&session->id);

    rwlock_wlock(&session_manager->lock);

    list_add(&session->hash_siblings, session_slot(session->id));
    ++session_manager->num_sessions;

    num_sessions = session_manager->num_sessions;

#ifndef NDEBUG
    locked_session = session;
#endif
    lock_lock(&session->lock);
    rwlock_wunlock(&session_manager->lock);

    if (num_sessions == 1) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_cleanup_event, &tv);
    }

    return session;
}

struct session *
session_new(void)
{
    crash_unsafe_enter();
    struct session *session = session_new_unsafe();
    if (session == NULL)
        crash_unsafe_leave();
    return session;
}

/**
 * After a while the dpool may have fragmentations, and memory is
 * wasted.  This function duplicates the session into a fresh dpool,
 * and frees the old session instance.  Of course, this requires that
 * there is enough free shared memory.
 */
static struct session * gcc_malloc
session_defragment(struct session *src)
{
    assert(crash_in_unsafe());

    struct dpool *pool;
    struct session *dest;

    pool = dpool_new(session_manager->shm);
    if (pool == NULL)
        return NULL;

    dest = session_dup(pool, src);
    if (dest == NULL) {
        dpool_destroy(pool);
        return src;
    }

    list_add(&dest->hash_siblings, session_slot(dest->id));
    ++session_manager->num_sessions;

    session_remove(src);
    return dest;
}

static struct session *
session_find(session_id_t id)
{
    if (session_manager->abandoned)
        return NULL;

    struct list_head *head = session_slot(id);
    struct session *session;

    assert(crash_in_unsafe());
    assert(locked_session == NULL);

    for (session = (struct session *)head->next;
         &session->hash_siblings != head;
         session = (struct session *)session->hash_siblings.next) {
        assert(session_slot(session->id) == head);

        if (session_id_equals(session->id, id)) {
#ifndef NDEBUG
            locked_session = session;
#endif
            lock_lock(&session->lock);

            session->expires = expiry_touch(session_manager->idle_timeout);
            ++session->counter;
            return session;
        }
    }

    return NULL;
}

struct session *
session_get(session_id_t id)
{
    struct session *session;

    assert(locked_session == NULL);

    crash_unsafe_enter();
    rwlock_rlock(&session_manager->lock);
    session = session_find(id);
    rwlock_runlock(&session_manager->lock);

    if (session == NULL)
        crash_unsafe_leave();

    return session;
}

static void
session_put_internal(struct session *session)
{
    assert(crash_in_unsafe());
    assert(session == locked_session);

    lock_unlock(&session->lock);

#ifndef NDEBUG
    locked_session = NULL;
#endif
}

static void
session_defragment_id(session_id_t id)
{
    assert(crash_in_unsafe());

    struct session *session = session_find(id);
    if (session == NULL)
        return;

    /* unlock the session, because session_defragment() may call
       session_remove(), and session_remove() expects the session to
       be unlocked.  This is ok, because we're holding the session
       manager lock at this point. */
    session_put_internal(session);

    session_defragment(session);
}

void
session_put(struct session *session)
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
session_delete(session_id_t id)
{
    struct session *session;

    assert(locked_session == NULL);

    crash_unsafe_enter();
    rwlock_wlock(&session_manager->lock);

    session = session_find(id);
    if (session != NULL) {
        session_put_internal(session);
        session_remove(session);
    }

    rwlock_wunlock(&session_manager->lock);
    crash_unsafe_leave();
}

bool
session_manager_visit(bool (*callback)(const struct session *session,
                                       void *ctx), void *ctx)
{
    bool result = true;

    crash_unsafe_enter();
    rwlock_rlock(&session_manager->lock);

    if (session_manager->abandoned) {
        rwlock_runlock(&session_manager->lock);
        crash_unsafe_leave();
        return false;
    }

    for (unsigned i = 0; i < SESSION_SLOTS && result; ++i) {
        struct list_head *slot = &session_manager->sessions[i];
        for (struct session *session = (struct session *)slot->next;
             &session->hash_siblings != slot && result;
             session = (struct session *)session->hash_siblings.next) {
            lock_lock(&session->lock);
            result = callback(session, ctx);
            lock_unlock(&session->lock);
        }
    }

    rwlock_runlock(&session_manager->lock);
    crash_unsafe_leave();

    return result;
}
