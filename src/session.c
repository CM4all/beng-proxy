/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session.h"
#include "cookie_jar.h"
#include "dpool.h"
#include "dhashmap.h"
#include "lock.h"
#include "expiry.h"
#include "crash.h"

#include <daemon/log.h>

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <event.h>

#define SESSION_TTL_NEW 120

struct session *
session_allocate(struct dpool *pool)
{
    struct timespec now;
    int ret;
    struct session *session;

    ret = clock_gettime(CLOCK_MONOTONIC, &now);
    if (ret < 0) {
        daemon_log(1, "clock_gettime(CLOCK_MONOTONIC) failed: %s\n",
                   strerror(errno));
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
    session->expires = now.tv_sec + SESSION_TTL_NEW;
    session->counter = 1;
    session->translate = NULL;
    session->widgets = NULL;
    session->cookies = cookie_jar_new(pool);

    return session;
}

void
session_destroy(struct session *session)
{
    lock_destroy(&session->lock);
    dpool_destroy(session->pool);
}

/**
 * Calculates the score for purging the session: higher score means
 * more likely to be purged.
 */
unsigned
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

void
session_clear_translate(struct session *session)
{
    assert(crash_in_unsafe());
    assert(session != NULL);

    if (session->translate != NULL) {
        d_free(session->pool, session->translate);
        session->translate = NULL;
    }
}

void
session_clear_user(struct session *session)
{
    assert(crash_in_unsafe());
    assert(session != NULL);

    if (session->user != NULL) {
        d_free(session->pool, session->user);
        session->user = NULL;
    }
}

void
session_clear_language(struct session *session)
{
    assert(crash_in_unsafe());
    assert(session != NULL);

    if (session->language != NULL) {
        d_free(session->pool, session->language);
        session->language = NULL;
    }
}

bool
session_set_translate(struct session *session, const char *translate)
{
    assert(crash_in_unsafe());
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
    assert(crash_in_unsafe());
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
    assert(crash_in_unsafe());
    assert(session != NULL);
    assert(language != NULL);

    if (session->language != NULL && strcmp(session->language, language) == 0)
        /* same value as before: no-op */
        return true;

    session_clear_language(session);

    session->language = d_strdup(session->pool, language);
    return session->language != NULL;
}

static struct dhashmap * gcc_malloc
widget_session_map_dup(struct dpool *pool, struct dhashmap *src,
                       struct session *session, struct widget_session *parent);

static struct widget_session * gcc_malloc
widget_session_dup(struct dpool *pool, const struct widget_session *src,
                   struct session *session)
{
    struct widget_session *dest;

    assert(crash_in_unsafe());
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

static struct dhashmap * gcc_malloc
widget_session_map_dup(struct dpool *pool, struct dhashmap *src,
                       struct session *session, struct widget_session *parent)
{
    assert(crash_in_unsafe());

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

struct session *
session_dup(struct dpool *pool, const struct session *src)
{
    struct session *dest;

    assert(crash_in_unsafe());

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

    if (src->realm != NULL)
        dest->realm = d_strdup(pool, src->realm);
    else
        dest->realm = NULL;

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

    return dest;
}

struct widget_session *
widget_session_allocate(struct session *session)
{
    struct widget_session *ws = d_malloc(session->pool, sizeof(*ws));
    if (ws == NULL)
        return NULL;

    ws->session = session;
    return ws;
}

static struct widget_session *
hashmap_r_get_widget_session(struct session *session, struct dhashmap **map_r,
                             const char *id, bool create)
{
    struct dhashmap *map;

    assert(crash_in_unsafe());
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
        if (map == NULL)
            return NULL;
    } else {
        struct widget_session *ws =
            (struct widget_session *)dhashmap_get(map, id);
        if (ws != NULL)
            return ws;

        if (!create)
            return NULL;
    }

    assert(create);

    struct widget_session *ws = widget_session_allocate(session);
    if (ws == NULL)
        return NULL;

    ws->parent = NULL;
    ws->id = d_strdup(session->pool, id);
    if (ws->id == NULL) {
        d_free(session->pool, ws);
        return NULL;
    }

    ws->children = NULL;
    ws->path_info = NULL;
    ws->query_string = NULL;

    dhashmap_put(map, ws->id, ws);
    return ws;
}

struct widget_session *
session_get_widget(struct session *session, const char *id, bool create)
{
    assert(crash_in_unsafe());
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
    assert(crash_in_unsafe());
    assert(parent != NULL);
    assert(parent->session != NULL);
    assert(id != NULL);

    return hashmap_r_get_widget_session(parent->session, &parent->children,
                                        id, create);
}

static void
widget_session_free(struct dpool *pool, struct widget_session *ws)
{
    assert(crash_in_unsafe());

    d_free(pool, ws->id);

    if (ws->path_info != NULL)
        d_free(pool, ws->path_info);

    if (ws->query_string != NULL)
        d_free(pool, ws->query_string);

    d_free(pool, ws);
}

static void
widget_session_clear_map(struct dpool *pool, struct dhashmap *map)
{
    assert(crash_in_unsafe());
    assert(pool != NULL);
    assert(map != NULL);

    while (true) {
        dhashmap_rewind(map);
        const struct dhashmap_pair *pair = dhashmap_next(map);
        if (pair == NULL)
            break;

        struct widget_session *ws = pair->value;
        dhashmap_remove(map, ws->id);

        widget_session_delete(pool, ws);
    }
}

void
widget_session_delete(struct dpool *pool, struct widget_session *ws)
{
    assert(crash_in_unsafe());
    assert(pool != NULL);
    assert(ws != NULL);

    if (ws->children != NULL)
        widget_session_clear_map(pool, ws->children);

    widget_session_free(pool, ws);
}

void
session_delete_widgets(struct session *session)
{
    assert(crash_in_unsafe());
    assert(session != NULL);

    if (session->widgets != NULL)
        widget_session_clear_map(session->pool, session->widgets);
}
