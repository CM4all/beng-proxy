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
    struct lock lock;

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

static void
session_destroy(struct session *session)
{
    lock_destroy(&session->lock);
    dpool_destroy(session->pool);
}

static void
session_remove(struct session *session)
{
    assert(lock_is_locked(&session_manager->lock));
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

    ret = clock_gettime(CLOCK_MONOTONIC, &now);
    if (ret < 0) {
        daemon_log(1, "clock_gettime(CLOCK_MONOTONIC) failed: %s\n",
                   strerror(errno));
        return;
    }

    lock_lock(&session_manager->lock);

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

    lock_unlock(&session_manager->lock);

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

    lock_init(&sm->lock);

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

    lock_lock(&sm->lock);

    for (i = 0; i < SESSION_SLOTS; ++i) {
        while (!list_empty(&sm->sessions[i])) {
            struct session *session = (struct session *)sm->sessions[i].next;
            session_remove(session);
        }
    }

    lock_unlock(&sm->lock);
    lock_destroy(&sm->lock);
}

void
session_manager_deinit(void)
{
    assert(session_manager != NULL);
    assert(session_manager->shm != NULL);

    event_del(&session_cleanup_event);

    if (refcount_put(&session_manager->ref) == 0)
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
    if (!session->cookie_received)
        return 30;

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

    lock_lock(&session_manager->lock);

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
        lock_unlock(&session_manager->lock);
        return false;
    }

    daemon_log(3, "purging %u sessions (score=%u)\n",
               num_sessions, highest_score);

    for (unsigned i = 0; i < num_sessions; ++i) {
        lock_lock(&sessions[i]->lock);
        session_remove(sessions[i]);
    }

    lock_unlock(&session_manager->lock);

    return true;
}

static struct list_head *
session_slot(session_id_t id)
{
    return &session_manager->sessions[id % SESSION_SLOTS];
}

session_id_t
session_id_parse(const char *p)
{
    unsigned long id;
    char *endptr;

    id = strtoul(p, &endptr, 16);
    if (id == 0 || *endptr != 0)
        return 0;

    return (session_id_t)id;
}

void
session_id_format(char dest[9], session_id_t id)
{
    format_uint32_hex_fixed(dest, id);
    dest[8] = 0;
}

static session_id_t
session_generate_id(void)
{

    return (session_id_t)random();
}

struct session *
session_new(void)
{
    struct timespec now;
    int ret;
    struct dpool *pool;
    struct session *session;
    unsigned num_sessions;

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
    session->id = session_generate_id();
    session->expires = now.tv_sec + SESSION_TTL_NEW;
    session->translate = NULL;
    session->widgets = NULL;
    session->cookies = cookie_jar_new(pool);

    lock_lock(&session_manager->lock);

    list_add(&session->hash_siblings, session_slot(session->id));
    ++session_manager->num_sessions;

    num_sessions = session_manager->num_sessions;

    lock_unlock(&session_manager->lock);

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
        const char *key = d_strdup(pool, pair->key);

        if (key == NULL)
            return NULL;

        dest_ws = widget_session_dup(pool, src_ws, session);
        if (dest_ws == NULL)
            return NULL;

        dhashmap_put(dest, key, dest_ws);
        dest_ws->parent = parent;
        dest_ws->session = session;
    }

    return dest;
}

static struct session * __attr_malloc
session_dup(struct dpool *pool, const struct session *src)
{
    struct session *dest;

    dest = d_malloc(pool, sizeof(*dest));
    if (dest == NULL)
        return NULL;

    dest->pool = pool;
    lock_init(&dest->lock);
    dest->id = src->id;
    dest->expires = src->expires;
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

    lock_lock(&session_manager->lock);
    list_add(&dest->hash_siblings, session_slot(dest->id));
    ++session_manager->num_sessions;
    lock_unlock(&session_manager->lock);

    return dest;
}

struct session * __attr_malloc
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

struct session *
session_get(session_id_t id)
{
    struct list_head *head = session_slot(id);
    struct session *session;

    lock_lock(&session_manager->lock);

    for (session = (struct session *)head->next;
         &session->hash_siblings != head;
         session = (struct session *)session->hash_siblings.next) {
        assert(session_slot(session->id) == head);

        if (session->id == id) {
            lock_unlock(&session_manager->lock);

            session->expires = expiry_touch(SESSION_TTL);
            return session;
        }
    }

    lock_unlock(&session_manager->lock);

    return NULL;
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
