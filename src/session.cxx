/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "session.hxx"
#include "cookie_jar.h"
#include "dpool.h"
#include "dhashmap.h"
#include "dbuffer.hxx"
#include "lock.h"
#include "expiry.h"
#include "crash.h"
#include "expiry.h"

#include <daemon/log.h>

#include <assert.h>
#include <stdlib.h>
#include <errno.h>
#include <string.h>
#include <event.h>

#define SESSION_TTL_NEW 120

struct session *
session_allocate(struct dpool *pool)
{
    struct session *session = (struct session *)d_malloc(pool, sizeof(*session));
    if (session == nullptr) {
        dpool_destroy(pool);
        return nullptr;
    }

    memset(session, 0, sizeof(*session));

    session->pool = pool;
    lock_init(&session->lock);
    session->expires = expiry_touch(SESSION_TTL_NEW);
    session->counter = 1;
    session->translate = nullptr;
    session->widgets = nullptr;
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
    if (session->is_new)
        return 1000;

    if (!session->cookie_received)
        return 50;

    if (session->user == nullptr)
        return 20;

    return 1;
}

void
session_clear_translate(struct session *session)
{
    assert(crash_in_unsafe());
    assert(session != nullptr);

    if (!session->translate.IsEmpty()) {
        d_free(session->pool, session->translate.data);
        session->translate = nullptr;
    }
}

void
session_clear_user(struct session *session)
{
    assert(crash_in_unsafe());
    assert(session != nullptr);

    if (session->user != nullptr) {
        d_free(session->pool, session->user);
        session->user = nullptr;
    }
}

void
session_clear_language(struct session *session)
{
    assert(crash_in_unsafe());
    assert(session != nullptr);

    if (session->language != nullptr) {
        d_free(session->pool, session->language);
        session->language = nullptr;
    }
}

bool
session_set_translate(struct session *session, ConstBuffer<void> translate)
{
    assert(crash_in_unsafe());
    assert(session != nullptr);
    assert(!translate.IsNull());

    if (!session->translate.IsNull() &&
        session->translate.size == translate.size &&
        memcmp(session->translate.data, translate.data, translate.size) == 0)
        /* same value as before: no-op */
        return true;

    session_clear_translate(session);

    session->translate = DupBuffer(session->pool, translate);
    return !session->translate.IsNull();
}

bool
session_set_user(struct session *session, const char *user, unsigned max_age)
{
    assert(crash_in_unsafe());
    assert(session != nullptr);
    assert(user != nullptr);

    if (session->user == nullptr || strcmp(session->user, user) != 0) {
        session_clear_user(session);

        session->user = d_strdup(session->pool, user);
        if (session->user == nullptr)
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
    assert(session != nullptr);
    assert(language != nullptr);

    if (session->language != nullptr && strcmp(session->language, language) == 0)
        /* same value as before: no-op */
        return true;

    session_clear_language(session);

    session->language = d_strdup(session->pool, language);
    return session->language != nullptr;
}

static struct dhashmap * gcc_malloc
widget_session_map_dup(struct dpool *pool, struct dhashmap *src,
                       struct session *session, struct widget_session *parent);

static struct widget_session * gcc_malloc
widget_session_dup(struct dpool *pool, const struct widget_session *src,
                   struct session *session)
{
    assert(crash_in_unsafe());
    assert(src != nullptr);
    assert(src->id != nullptr);

    struct widget_session *dest = (struct widget_session *)d_malloc(pool, sizeof(*dest));
    if (dest == nullptr)
        return nullptr;

    dest->id = d_strdup(pool, src->id);
    if (dest->id == nullptr)
        return nullptr;

    if (src->children != nullptr) {
        dest->children = widget_session_map_dup(pool, src->children,
                                                session, dest);
        if (dest->children == nullptr)
            return nullptr;
    } else
        dest->children = nullptr;

    if (src->path_info != nullptr) {
        dest->path_info = d_strdup(pool, src->path_info);
        if (dest->path_info == nullptr)
            return nullptr;
    } else
        dest->path_info = nullptr;

    if (src->query_string != nullptr) {
        dest->query_string = d_strdup(pool, src->query_string);
        if (dest->query_string == nullptr)
            return nullptr;
    } else
        dest->query_string = nullptr;

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
    if (dest == nullptr)
        return nullptr;

    dhashmap_rewind(src);
    while ((pair = dhashmap_next(src)) != nullptr) {
        const struct widget_session *src_ws = (const struct widget_session *)
            pair->value;
        struct widget_session *dest_ws;

        dest_ws = widget_session_dup(pool, src_ws, session);
        if (dest_ws == nullptr)
            return nullptr;

        dhashmap_put(dest, dest_ws->id, dest_ws);
        dest_ws->parent = parent;
        dest_ws->session = session;
    }

    return dest;
}

struct session *
session_dup(struct dpool *pool, const struct session *src)
{
    assert(crash_in_unsafe());

    struct session *dest = (struct session *)d_malloc(pool, sizeof(*dest));
    if (dest == nullptr)
        return nullptr;

    dest->pool = pool;
    lock_init(&dest->lock);
    dest->id = src->id;
    dest->expires = src->expires;
    dest->counter = 1;
    dest->cookie_sent = src->cookie_sent;
    dest->cookie_received = src->cookie_received;

    if (src->realm != nullptr)
        dest->realm = d_strdup(pool, src->realm);
    else
        dest->realm = nullptr;

    dest->translate = DupBuffer(pool, src->translate);

    if (src->user != nullptr)
        dest->user = d_strdup(pool, src->user);
    else
        dest->user = nullptr;

    if (src->language != nullptr)
        dest->language = d_strdup(pool, src->language);
    else
        dest->language = nullptr;

    if (src->widgets != nullptr) {
        dest->widgets = widget_session_map_dup(pool, src->widgets, dest, nullptr);
        if (dest->widgets == nullptr) {
            return nullptr;
        }
    } else
        dest->widgets = nullptr;

    dest->cookies = cookie_jar_dup(pool, src->cookies);

    return dest;
}

struct widget_session *
widget_session_allocate(struct session *session)
{
    struct widget_session *ws = (struct widget_session *)d_malloc(session->pool, sizeof(*ws));
    if (ws == nullptr)
        return nullptr;

    ws->session = session;
    return ws;
}

static struct widget_session *
hashmap_r_get_widget_session(struct session *session, struct dhashmap **map_r,
                             const char *id, bool create)
{
    struct dhashmap *map;

    assert(crash_in_unsafe());
    assert(session != nullptr);
    assert(lock_is_locked(&session->lock));
    assert(map_r != nullptr);
    assert(id != nullptr);

    map = *map_r;
    if (map == nullptr) {
        if (!create)
            return nullptr;

        /* lazy initialisation */
        *map_r = map = dhashmap_new(session->pool, 16);
        if (map == nullptr)
            return nullptr;
    } else {
        struct widget_session *ws =
            (struct widget_session *)dhashmap_get(map, id);
        if (ws != nullptr)
            return ws;

        if (!create)
            return nullptr;
    }

    assert(create);

    struct widget_session *ws = widget_session_allocate(session);
    if (ws == nullptr)
        return nullptr;

    ws->parent = nullptr;
    ws->id = d_strdup(session->pool, id);
    if (ws->id == nullptr) {
        d_free(session->pool, ws);
        return nullptr;
    }

    ws->children = nullptr;
    ws->path_info = nullptr;
    ws->query_string = nullptr;

    dhashmap_put(map, ws->id, ws);
    return ws;
}

struct widget_session *
session_get_widget(struct session *session, const char *id, bool create)
{
    assert(crash_in_unsafe());
    assert(session != nullptr);
    assert(id != nullptr);

    return hashmap_r_get_widget_session(session, &session->widgets, id,
                                        create);
}

struct widget_session *
widget_session_get_child(struct widget_session *parent,
                         const char *id,
                         bool create)
{
    assert(crash_in_unsafe());
    assert(parent != nullptr);
    assert(parent->session != nullptr);
    assert(id != nullptr);

    return hashmap_r_get_widget_session(parent->session, &parent->children,
                                        id, create);
}

static void
widget_session_free(struct dpool *pool, struct widget_session *ws)
{
    assert(crash_in_unsafe());

    d_free(pool, ws->id);

    if (ws->path_info != nullptr)
        d_free(pool, ws->path_info);

    if (ws->query_string != nullptr)
        d_free(pool, ws->query_string);

    d_free(pool, ws);
}

static void
widget_session_clear_map(struct dpool *pool, struct dhashmap *map)
{
    assert(crash_in_unsafe());
    assert(pool != nullptr);
    assert(map != nullptr);

    while (true) {
        dhashmap_rewind(map);
        const struct dhashmap_pair *pair = dhashmap_next(map);
        if (pair == nullptr)
            break;

        struct widget_session *ws = (struct widget_session *)pair->value;
        dhashmap_remove(map, ws->id);

        widget_session_delete(pool, ws);
    }
}

void
widget_session_delete(struct dpool *pool, struct widget_session *ws)
{
    assert(crash_in_unsafe());
    assert(pool != nullptr);
    assert(ws != nullptr);

    if (ws->children != nullptr)
        widget_session_clear_map(pool, ws->children);

    widget_session_free(pool, ws);
}

void
session_delete_widgets(struct session *session)
{
    assert(crash_in_unsafe());
    assert(session != nullptr);

    if (session->widgets != nullptr)
        widget_session_clear_map(session->pool, session->widgets);
}
