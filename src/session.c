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
}

void
session_manager_init(pool_t pool)
{
    struct timeval tv;

    assert(session_manager.pool == NULL);

    session_manager.pool = pool_new_libc(pool, "session_manager");
    list_init(&session_manager.sessions);

    tv = cleanup_interval;
    evtimer_set(&session_manager.cleanup_event, cleanup_event_callback, NULL);
    evtimer_add(&session_manager.cleanup_event, &tv);
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
    snprintf(dest, sizeof(dest), "%08x", id);
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

    list_add(&session->hash_siblings, &session_manager.sessions);

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

    list_remove(&session->hash_siblings);
    session->removed = 1;
    pool_unref(pool);
}
