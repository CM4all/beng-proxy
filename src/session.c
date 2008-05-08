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

#include <daemon/log.h>

#include <assert.h>
#include <stdlib.h>
#include <event.h>

#define SESSION_TTL_NEW 120
#define SESSION_TTL 600

#define SESSION_SLOTS 1024

static struct timeval cleanup_interval = {
    .tv_sec = 60,
    .tv_usec = 0,
};

static struct {
    struct shm *shm;

    struct list_head sessions[SESSION_SLOTS];
    unsigned num_sessions;

    struct event cleanup_event;
} session_manager;

static void
session_remove(struct session *session);

static void
cleanup_event_callback(int fd __attr_unused, short event __attr_unused,
                       void *ctx __attr_unused)
{
    time_t now = time(NULL);
    unsigned i;
    struct session *session, *next;

    for (i = 0; i < SESSION_SLOTS; ++i) {
        for (session = (struct session *)session_manager.sessions[i].next;
             &session->hash_siblings != &session_manager.sessions[i];
             session = next) {
            next = (struct session *)session->hash_siblings.next;
            if (now >= session->expires)
                session_remove(session);
        }
    }

    if (session_manager.num_sessions > 0) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_manager.cleanup_event, &tv);
    }
}

void
session_manager_init(void)
{
    unsigned i;

    assert(session_manager.shm == NULL);

    srandom((unsigned)time(NULL));

    session_manager.shm = shm_new(4096, 8192);
    if (session_manager.shm == NULL) {
        daemon_log(1, "shm_new() failed\n");
        abort();
    }

    for (i = 0; i < SESSION_SLOTS; ++i)
        list_init(&session_manager.sessions[i]);

    evtimer_set(&session_manager.cleanup_event, cleanup_event_callback, NULL);
}

void
session_manager_deinit(void)
{
    unsigned i;

    assert(session_manager.shm != NULL);

    event_del(&session_manager.cleanup_event);

    for (i = 0; i < SESSION_SLOTS; ++i) {
        while (!list_empty(&session_manager.sessions[i])) {
            struct session *session = (struct session *)session_manager.sessions[i].next;
            session_remove(session);
        }
    }

    shm_close(session_manager.shm);
    session_manager.shm = NULL;
}

static struct list_head *
session_slot(session_id_t id)
{
    return &session_manager.sessions[id % SESSION_SLOTS];
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

    return (session_id_t)random(); /* XXX this is insecure! */
}

struct session *
session_new(void)
{
    struct dpool *pool;
    struct session *session;

    pool = dpool_new(session_manager.shm);
    if (pool == NULL)
        return NULL;

    session = d_malloc(pool, sizeof(*session));
    if (session == NULL) {
        dpool_destroy(pool);
        return NULL;
    }

    memset(session, 0, sizeof(*session));

    session->pool = pool;
    session->uri_id = session_generate_id();
    session->cookie_id = session_generate_id();
    session->expires = time(NULL) + SESSION_TTL_NEW;
    session->translate = NULL;
    session->widgets = NULL;
    session->cookies = cookie_jar_new(pool);

    list_add(&session->hash_siblings, session_slot(session->uri_id));
    ++session_manager.num_sessions;

    if (session_manager.num_sessions == 1) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_manager.cleanup_event, &tv);
    }

    return session;
}

struct session *
session_get(session_id_t id)
{
    struct list_head *head = session_slot(id);
    struct session *session;

    for (session = (struct session *)head->next;
         &session->hash_siblings != head;
         session = (struct session *)session->hash_siblings.next) {
        assert(session_slot(session->uri_id) == head);

        if (session->uri_id == id) {
            session->expires = time(NULL) + SESSION_TTL;
            return session;
        }
    }

    return NULL;
}

static void
session_remove(struct session *session)
{
    assert(session_manager.num_sessions > 0);

    list_remove(&session->hash_siblings);
    --session_manager.num_sessions;

    if (session_manager.num_sessions == 0)
        evtimer_del(&session_manager.cleanup_event);

    dpool_destroy(session->pool);
}

static struct widget_session *
hashmap_r_get_widget_session(struct session *session, struct dhashmap **map_r,
                             const char *id, bool create)
{
    struct dhashmap *map;
    struct widget_session *ws;

    assert(session != NULL);
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
    ws->pool = session->pool;
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
