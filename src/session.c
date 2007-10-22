/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session.h"

#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <event.h>

#define SESSION_TTL_NEW 120
#define SESSION_TTL 600

static struct timeval cleanup_interval = {
    .tv_sec = 60,
    .tv_usec = 0,
};

struct {
    pool_t pool;
    struct list_head sessions; /* XXX use hash table */
    unsigned num_sessions;
    struct event cleanup_event;
} session_manager;

static void
cleanup_event_callback(int fd, short event, void *ctx)
{
    time_t now = time(NULL);
    session_t session, next;

    (void)fd;
    (void)event;
    (void)ctx;

    for (session = (session_t)session_manager.sessions.next;
         session != (session_t)&session_manager.sessions;
         session = next) {
        next = (session_t)session->hash_siblings.next;
        if (now >= session->expires)
            session_remove(session);
    }

    if (session_manager.num_sessions > 0) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_manager.cleanup_event, &tv);
    }
}

void
session_manager_init(pool_t pool)
{
    assert(session_manager.pool == NULL);

    session_manager.pool = pool_new_libc(pool, "session_manager");
    list_init(&session_manager.sessions);

    evtimer_set(&session_manager.cleanup_event, cleanup_event_callback, NULL);
}

void
session_manager_deinit(void)
{
    assert(session_manager.pool != NULL);

    event_del(&session_manager.cleanup_event);

    while (session_manager.sessions.next != &session_manager.sessions) {
        session_t session = (session_t)session_manager.sessions.next;
        session_remove(session);
    }

    pool_unref(session_manager.pool);
    session_manager.pool = NULL;
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
    snprintf(dest, 9, "%08x", id);
}

static session_id_t
session_generate_id(void)
{

    return (session_id_t)random(); /* XXX this is insecure! */
}

session_t
session_new(void)
{
    pool_t pool = pool_new_libc(session_manager.pool, "session");
    session_t session = p_calloc(pool, sizeof(*session));

    session->pool = pool;
    session->id = session_generate_id();
    session->expires = time(NULL) + SESSION_TTL_NEW;
    session->widgets = NULL;

    list_add(&session->hash_siblings, &session_manager.sessions);
    ++session_manager.num_sessions;

    if (session_manager.num_sessions == 1) {
        struct timeval tv = cleanup_interval;
        evtimer_add(&session_manager.cleanup_event, &tv);
    }

    return session;
}

session_t
session_get(session_id_t id)
{
    session_t session;

    for (session = (session_t)session_manager.sessions.next;
         session != (session_t)&session_manager.sessions;
         session = (session_t)session->hash_siblings.next) {
        if (session->id == id) {
            session->expires = time(NULL) + SESSION_TTL;
            return session;
        }
    }

    return NULL;
}

void
session_remove(session_t session)
{
    pool_t pool = session->pool;

    if (session->removed)
        return;

    assert(session_manager.num_sessions > 0);

    list_remove(&session->hash_siblings);
    --session_manager.num_sessions;

    if (session_manager.num_sessions == 0)
        evtimer_del(&session_manager.cleanup_event);

    session->removed = 1;
    pool_unref(pool);
}

static struct widget_session *
hashmap_r_get_widget_session(pool_t pool, hashmap_t *map_r,
                             const char *id, int create)
{
    hashmap_t map = *map_r;
    struct widget_session *ws;

    assert(pool != NULL);
    assert(map_r != NULL);
    assert(id != NULL);

    if (map == NULL) {
        if (!create)
            return NULL;

        /* lazy initialisation */
        *map_r = map = hashmap_new(pool, 16);
    } else {
        ws = (struct widget_session *)hashmap_get(map, id);
        if (ws != NULL)
            return ws;

        if (!create)
            return NULL;
    }

    ws = p_malloc(pool, sizeof(*ws));
    ws->parent = NULL;
    ws->pool = pool;
    ws->id = p_strdup(pool, id);
    ws->children = NULL;
    ws->query_string = NULL;

    hashmap_addn(map, ws->id, ws);
    return ws;
}

struct widget_session *
session_get_widget(session_t session, const char *id, int create)
{
    assert(session != NULL);
    assert(id != NULL);

    return hashmap_r_get_widget_session(session->pool, &session->widgets,
                                        id, create);
}

struct widget_session *
widget_session_get_child(struct widget_session *parent, const char *id,
                         int create)
{
    assert(parent != NULL);
    assert(id != NULL);

    return hashmap_r_get_widget_session(parent->pool, &parent->children,
                                        id, create);
}
