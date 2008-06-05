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

#include <daemon/log.h>

#include <assert.h>
#include <stdlib.h>
#include <event.h>

#define SHM_PAGE_SIZE 4096
#define SHM_NUM_PAGES 8192
#define SM_PAGES ((sizeof(struct session_manager) + SHM_PAGE_SIZE - 1) / SHM_PAGE_SIZE)

#define SESSION_TTL_NEW 120
#define SESSION_TTL 600

#define SESSION_SLOTS 1024

struct session_manager {
    struct refcount ref;

    struct shm *shm;

    /** this lock protects the following hash table */
    struct lock lock;

    struct list_head sessions[SESSION_SLOTS];
    unsigned num_sessions;
};

static const struct timeval cleanup_interval = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static struct session_manager *session_manager;
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
    assert(session_manager->num_sessions > 0);

    lock_lock(&session_manager->lock);

    list_remove(&session->hash_siblings);
    --session_manager->num_sessions;

    lock_unlock(&session_manager->lock);

    if (session_manager->num_sessions == 0)
        evtimer_del(&session_cleanup_event);

    session_destroy(session);
}

static void
cleanup_event_callback(int fd __attr_unused, short event __attr_unused,
                       void *ctx __attr_unused)
{
    time_t now = time(NULL);
    unsigned i;
    struct session *session, *next;

    lock_lock(&session_manager->lock);

    for (i = 0; i < SESSION_SLOTS; ++i) {
        for (session = (struct session *)session_manager->sessions[i].next;
             &session->hash_siblings != &session_manager->sessions[i];
             session = next) {
            next = (struct session *)session->hash_siblings.next;
            if (now >= session->expires)
                session_remove(session);
        }
    }

    lock_unlock(&session_manager->lock);

    if (session_manager->num_sessions > 0) {
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

void
session_manager_init(void)
{
    struct timeval tv;

    gettimeofday(&tv, NULL);
    srandom(tv.tv_sec ^ tv.tv_usec);

    if (session_manager == NULL)
        session_manager = session_manager_new();
    else {
        refcount_get(&session_manager->ref);
        shm_ref(session_manager->shm);
    }

    evtimer_set(&session_cleanup_event, cleanup_event_callback, NULL);
}

static void
session_manager_destroy(struct session_manager *sm)
{
    unsigned i;

    for (i = 0; i < SESSION_SLOTS; ++i) {
        while (!list_empty(&sm->sessions[i])) {
            struct session *session = (struct session *)sm->sessions[i].next;
            session_remove(session);
        }
    }

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
    struct dpool *pool;
    struct session *session;

    pool = dpool_new(session_manager->shm);
    if (pool == NULL)
        return NULL;

    session = d_malloc(pool, sizeof(*session));
    if (session == NULL) {
        dpool_destroy(pool);
        return NULL;
    }

    memset(session, 0, sizeof(*session));

    session->pool = pool;
    lock_init(&session->lock);
    session->uri_id = session_generate_id();
    session->cookie_id = session_generate_id();
    session->expires = time(NULL) + SESSION_TTL_NEW;
    session->translate = NULL;
    session->widgets = NULL;
    session->cookies = cookie_jar_new(pool);

    lock_lock(&session_manager->lock);

    list_add(&session->hash_siblings, session_slot(session->uri_id));
    ++session_manager->num_sessions;

    lock_unlock(&session_manager->lock);

    if (session_manager->num_sessions == 1) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_cleanup_event, &tv);
    }

    return session;
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

struct session * __attr_malloc
session_dup(const struct session *src)
{
    struct dpool *pool;
    struct session *dest;

    pool = dpool_new(session_manager->shm);
    if (pool == NULL)
        return NULL;

    dest = d_malloc(pool, sizeof(*dest));
    if (dest == NULL) {
        dpool_destroy(pool);
        return NULL;
    }

    dest->pool = pool;
    lock_init(&dest->lock);
    dest->uri_id = src->uri_id;
    dest->cookie_id = src->cookie_id;
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
            dpool_destroy(pool);
            return NULL;
        }
    } else
        dest->widgets = NULL;

    dest->cookies = cookie_jar_dup(pool, src->cookies);

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
        assert(session_slot(session->uri_id) == head);

        if (session->uri_id == id) {
            lock_unlock(&session_manager->lock);

            session->expires = time(NULL) + SESSION_TTL;
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
