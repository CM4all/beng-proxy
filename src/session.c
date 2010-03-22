/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session.h"
#include "format.h"
#include "cookie-client.h"
#include "shm.h"
#include "dpool.h"
#include "dhashmap.h"
#include "lock.h"
#include "rwlock.h"
#include "refcount.h"
#include "expiry.h"

#include <daemon/log.h>

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <event.h>

#define SHM_PAGE_SIZE 4096
#define SHM_NUM_PAGES 32768
#define SM_PAGES ((sizeof(struct session_manager) + SHM_PAGE_SIZE - 1) / SHM_PAGE_SIZE)

#define SESSION_TTL_NEW 120
#define SESSION_TTL 600

#define SESSION_SLOTS 16381

struct session_manager {
    struct refcount ref;

    struct shm *shm;

    /** this lock protects the following hash table */
    struct rwlock lock;

    struct list_head sessions[SESSION_SLOTS];
    unsigned num_sessions;
};

/** clean up expired sessions every 60 seconds */
static const struct timeval cleanup_interval = {
    .tv_sec = 60,
    .tv_usec = 0,
};

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
session_destroy(struct session *session)
{
    lock_destroy(&session->lock);
    dpool_destroy(session->pool);
}

static void
session_remove(struct session *session)
{
    assert(rwlock_is_wlocked(&session_manager->lock));
    assert(session_manager->num_sessions > 0);

    list_remove(&session->hash_siblings);
    --session_manager->num_sessions;

    if (session_manager->num_sessions == 0)
        evtimer_del(&session_cleanup_event);

    session_destroy(session);
}

static void
cleanup_event_callback(int fd __attr_unused, short event __attr_unused,
                       void *ctx __attr_unused)
{
    struct timespec now;
    int ret;
    unsigned i;
    struct session *session, *next;
    bool non_empty;

    assert(locked_session == NULL);

    ret = clock_gettime(CLOCK_MONOTONIC, &now);
    if (ret < 0) {
        daemon_log(1, "clock_gettime(CLOCK_MONOTONIC) failed: %s\n",
                   strerror(errno));
        return;
    }

    rwlock_wlock(&session_manager->lock);

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

    if (non_empty) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_cleanup_event, &tv);
    }
}

static struct session_manager *
session_manager_new(void)
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
    sm->shm = shm;

    rwlock_init(&sm->lock);

    for (i = 0; i < SESSION_SLOTS; ++i)
        list_init(&sm->sessions[i]);

    sm->num_sessions = 0;

    return sm;
}

bool
session_manager_init(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    srandom(tv.tv_sec ^ tv.tv_usec);

    if (session_manager == NULL) {
        session_manager = session_manager_new();
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

    rwlock_wlock(&sm->lock);

    for (i = 0; i < SESSION_SLOTS; ++i) {
        while (!list_empty(&sm->sessions[i])) {
            struct session *session = (struct session *)sm->sessions[i].next;
            session_remove(session);
        }
    }

    rwlock_wunlock(&sm->lock);
    rwlock_destroy(&sm->lock);
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

/**
 * Calculates the score for purging the session: higher score means
 * more likely to be purged.
 */
static unsigned
session_purge_score(const struct session *session)
{
    if (session->new)
        return 1000;

    if (!session->cookie_received)
        return 50;

    if (session->user == NULL)
        return 20;

    return 1;
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
        return false;
    }

    daemon_log(3, "purging %u sessions (score=%u)\n",
               num_sessions, highest_score);

    for (unsigned i = 0; i < num_sessions; ++i) {
        lock_lock(&sessions[i]->lock);
        session_remove(sessions[i]);
    }

    rwlock_wunlock(&session_manager->lock);

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

bool
session_id_parse(const char *p, session_id_t *id_r)
{
#ifdef SESSION_ID_WORDS
    char segment[9];
    session_id_t id;
    char *endptr;

    if (strlen(p) != SESSION_ID_WORDS * 8)
        return false;

    segment[8] = 0;
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i) {
        memcpy(segment, p + i * 8, 8);
        id.data[i] = strtoul(segment, &endptr, 16);
        if (endptr != segment + 8)
            return false;
    }

    *id_r = id;
#else
    guint64 id;
    char *endptr;

    id = g_ascii_strtoull(p, &endptr, 16);
    if (id == 0 || *endptr != 0)
        return false;

    *id_r = (session_id_t)id;
#endif

    return true;
}

const char *
session_id_format(session_id_t id, struct session_id_string *string)
{
#ifdef SESSION_ID_WORDS
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i)
        format_uint32_hex_fixed(string->buffer + i * 8, id.data[i]);
#else
    format_uint64_hex_fixed(string->buffer, id);
#endif
    string->buffer[sizeof(string->buffer) - 1] = 0;
    return string->buffer;
}

static void
session_generate_id(session_id_t *id_r)
{
#ifdef SESSION_ID_WORDS
    for (unsigned i = 0; i < SESSION_ID_WORDS; ++i)
        id_r->data[i] = random();
#else
    *id_r = (session_id_t)random() | (session_id_t)random() << 32;
#endif
}

struct session *
session_new(void)
{
    struct timespec now;
    int ret;
    struct dpool *pool;
    struct session *session;
    unsigned num_sessions;

    assert(locked_session == NULL);

    ret = clock_gettime(CLOCK_MONOTONIC, &now);
    if (ret < 0) {
        daemon_log(1, "clock_gettime(CLOCK_MONOTONIC) failed: %s\n",
                   strerror(errno));
        return NULL;
    }

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

    session = d_malloc(pool, sizeof(*session));
    if (session == NULL) {
        dpool_destroy(pool);
        return NULL;
    }

    memset(session, 0, sizeof(*session));

    session->pool = pool;
    lock_init(&session->lock);
    session_generate_id(&session->id);
    session->expires = now.tv_sec + SESSION_TTL_NEW;
    session->counter = 1;
    session->translate = NULL;
    session->widgets = NULL;
    session->cookies = cookie_jar_new(pool);

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

void
session_clear_translate(struct session *session)
{
    assert(session != NULL);

    if (session->translate != NULL) {
        d_free(session->pool, session->translate);
        session->translate = NULL;
    }
}

void
session_clear_user(struct session *session)
{
    assert(session != NULL);

    if (session->user != NULL) {
        d_free(session->pool, session->user);
        session->user = NULL;
    }
}

void
session_clear_language(struct session *session)
{
    assert(session != NULL);

    if (session->language != NULL) {
        d_free(session->pool, session->language);
        session->language = NULL;
    }
}

bool
session_set_translate(struct session *session, const char *translate)
{
    assert(session != NULL);
    assert(translate != NULL);

    if (session->translate != NULL && strcmp(session->translate, translate) == 0)
        /* same value as before: no-op */
        return true;

    session_clear_translate(session);

    session->translate = d_strdup(session->pool, translate);
    return session->translate != NULL;
}

bool
session_set_user(struct session *session, const char *user, unsigned max_age)
{
    assert(session != NULL);
    assert(user != NULL);

    if (session->user == NULL || strcmp(session->user, user) != 0) {
        session_clear_user(session);

        session->user = d_strdup(session->pool, user);
        if (session->user == NULL)
            return false;
    }

    if (max_age == (unsigned)-1)
        /* never expires */
        session->user_expires = 0;
    else if (max_age == 0)
        /* expires immediately, use only once */
        session->user_expires = 1;
    else
        session->user_expires = expiry_touch(max_age);

    return true;
}

bool
session_set_language(struct session *session, const char *language)
{
    assert(session != NULL);
    assert(language != NULL);

    if (session->language != NULL && strcmp(session->language, language) == 0)
        /* same value as before: no-op */
        return true;

    session_clear_language(session);

    session->language = d_strdup(session->pool, language);
    return session->language != NULL;
}

static struct dhashmap * __attr_malloc
widget_session_map_dup(struct dpool *pool, struct dhashmap *src,
                       struct session *session, struct widget_session *parent);

static struct widget_session * __attr_malloc
widget_session_dup(struct dpool *pool, const struct widget_session *src,
                   struct session *session)
{
    struct widget_session *dest;

    assert(src != NULL);
    assert(src->id != NULL);

    dest = d_malloc(pool, sizeof(*dest));
    if (dest == NULL)
        return NULL;

    dest->id = d_strdup(pool, src->id);
    if (dest->id == NULL)
        return NULL;

    if (src->children != NULL) {
        dest->children = widget_session_map_dup(pool, src->children,
                                                session, dest);
        if (dest->children == NULL)
            return NULL;
    } else
        dest->children = NULL;

    if (src->path_info != NULL) {
        dest->path_info = d_strdup(pool, src->path_info);
        if (dest->path_info == NULL)
            return NULL;
    } else
        dest->path_info = NULL;

    if (src->query_string != NULL) {
        dest->query_string = d_strdup(pool, src->query_string);
        if (dest->query_string == NULL)
            return NULL;
    } else
        dest->query_string = NULL;

    return dest;
}

static struct dhashmap * __attr_malloc
widget_session_map_dup(struct dpool *pool, struct dhashmap *src,
                       struct session *session, struct widget_session *parent)
{
    struct dhashmap *dest;
    const struct dhashmap_pair *pair;

    dest = dhashmap_new(pool, 16);
    if (dest == NULL)
        return NULL;

    dhashmap_rewind(src);
    while ((pair = dhashmap_next(src)) != NULL) {
        const struct widget_session *src_ws = pair->value;
        struct widget_session *dest_ws;

        dest_ws = widget_session_dup(pool, src_ws, session);
        if (dest_ws == NULL)
            return NULL;

        dhashmap_put(dest, dest_ws->id, dest_ws);
        dest_ws->parent = parent;
        dest_ws->session = session;
    }

    return dest;
}

static struct session * __attr_malloc
session_dup(struct dpool *pool, const struct session *src)
{
    struct session *dest;

    assert(locked_session == NULL);

    dest = d_malloc(pool, sizeof(*dest));
    if (dest == NULL)
        return NULL;

    dest->pool = pool;
    lock_init(&dest->lock);
    dest->id = src->id;
    dest->expires = src->expires;
    dest->counter = 1;
    dest->cookie_sent = src->cookie_sent;
    dest->cookie_received = src->cookie_received;

    if (src->translate != NULL)
        dest->translate = d_strdup(pool, src->translate);
    else
        dest->translate = NULL;

    if (src->user != NULL)
        dest->user = d_strdup(pool, src->user);
    else
        dest->user = NULL;

    if (src->language != NULL)
        dest->language = d_strdup(pool, src->language);
    else
        dest->language = NULL;

    if (src->widgets != NULL) {
        dest->widgets = widget_session_map_dup(pool, src->widgets, dest, NULL);
        if (dest->widgets == NULL) {
            return NULL;
        }
    } else
        dest->widgets = NULL;

    dest->cookies = cookie_jar_dup(pool, src->cookies);

    list_add(&dest->hash_siblings, session_slot(dest->id));
    ++session_manager->num_sessions;

    return dest;
}

/**
 * After a while the dpool may have fragmentations, and memory is
 * wasted.  This function duplicates the session into a fresh dpool,
 * and frees the old session instance.  Of course, this requires that
 * there is enough free shared memory.
 */
static struct session * __attr_malloc
session_defragment(struct session *src)
{
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

    session_remove(src);
    return dest;
}

static inline bool
session_id_equals(const session_id_t a, const session_id_t b)
{
#ifdef SESSION_ID_WORDS
    return memcmp(&a, &b, sizeof(a)) == 0;
#else
    return a == b;
#endif
}

static struct session *
session_find(session_id_t id)
{
    struct list_head *head = session_slot(id);
    struct session *session;

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

            session->expires = expiry_touch(SESSION_TTL);
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

    rwlock_rlock(&session_manager->lock);
    session = session_find(id);
    rwlock_runlock(&session_manager->lock);

    return session;
}

static void
session_put_internal(struct session *session)
{
    assert(session == locked_session);

    lock_unlock(&session->lock);

#ifndef NDEBUG
    locked_session = NULL;
#endif
}

static void
session_defragment_id(session_id_t id)
{
    struct session *session = session_find(id);
    if (session_find == NULL)
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
}

void
session_delete(session_id_t id)
{
    struct session *session;

    assert(locked_session == NULL);

    rwlock_wlock(&session_manager->lock);

    session = session_find(id);
    if (session != NULL) {
        session_put_internal(session);
        session_remove(session);
    }

    rwlock_wunlock(&session_manager->lock);
}

static struct widget_session *
hashmap_r_get_widget_session(struct session *session, struct dhashmap **map_r,
                             const char *id, bool create)
{
    struct dhashmap *map;
    struct widget_session *ws;

    assert(session != NULL);
    assert(lock_is_locked(&session->lock));
    assert(map_r != NULL);
    assert(id != NULL);

    map = *map_r;
    if (map == NULL) {
        if (!create)
            return NULL;

        /* lazy initialisation */
        *map_r = map = dhashmap_new(session->pool, 16);
    } else {
        ws = (struct widget_session *)dhashmap_get(map, id);
        if (ws != NULL)
            return ws;

        if (!create)
            return NULL;
    }

    assert(create);

    ws = d_malloc(session->pool, sizeof(*ws));
    if (ws == NULL)
        return NULL;

    ws->parent = NULL;
    ws->session = session;
    ws->id = d_strdup(session->pool, id);
    ws->children = NULL;
    ws->path_info = NULL;
    ws->query_string = NULL;

    dhashmap_put(map, ws->id, ws);
    return ws;
}

struct widget_session *
session_get_widget(struct session *session, const char *id, bool create)
{
    assert(session != NULL);
    assert(id != NULL);

    return hashmap_r_get_widget_session(session, &session->widgets, id,
                                        create);
}

struct widget_session *
widget_session_get_child(struct widget_session *parent,
                         const char *id,
                         bool create)
{
    assert(parent != NULL);
    assert(parent->session != NULL);
    assert(id != NULL);

    return hashmap_r_get_widget_session(parent->session, &parent->children,
                                        id, create);
}
