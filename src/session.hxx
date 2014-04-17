/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SESSION_HXX
#define BENG_PROXY_SESSION_HXX

#include "lock.h"
#include "session_id.h"

#include <inline/list.h>
#include <inline/compiler.h>

#include <time.h>
#include <stdint.h>

#ifdef SESSION_ID_SIZE
#include <string.h> /* for memset() */
#endif

struct dpool;
struct dhashmap;

/**
 * Session data associated with a widget instance (struct widget).
 */
struct widget_session {
    struct widget_session *parent;

    struct session *session;

    /** local id of this widget; must not be nullptr since widgets
        without an id cannot have a session */
    const char *id;
    struct dhashmap *children;

    /** last relative URI */
    char *path_info;

    /** last query string */
    char *query_string;
};

/**
 * A session associated with a user.
 */
struct session {
    struct list_head hash_siblings;

    struct dpool *pool;

    /** this lock protects the bit fields, all widget session hash
        maps and the cookie jar */
    struct lock lock;

    /** identification number of this session */
    session_id_t id;

    /** when will this session expire? */
    time_t expires;

    /**
     * Counts how often this session has been used.
     */
    unsigned counter;

    /** is this a new session, i.e. there hasn't been a second request
        yet? */
    bool is_new;

    /** has a HTTP cookie with this session id already been sent? */
    bool cookie_sent;

    /** has a HTTP cookie with this session id already been received? */
    bool cookie_received;

    /**
     * The name of this session's realm.  It is always non-nullptr.
     */
    const char *realm;

    /** an opaque string for the translation server */
    const char *translate;

    /** the user name which is logged in (nullptr if anonymous), provided
        by the translation server */
    const char *user;

    /** when will the #user attribute expire? */
    time_t user_expires;

    /** optional  for the "Accept-Language" header, provided
        by the translation server */
    const char *language;

    /** a map of widget path to struct widget_session */
    struct dhashmap *widgets;

    /** all cookies received by widget servers */
    struct cookie_jar *cookies;
};

gcc_malloc
struct session *
session_allocate(struct dpool *pool);

gcc_malloc
struct session *
session_dup(struct dpool *pool, const struct session *src);

void
session_destroy(struct session *session);

gcc_pure
unsigned
session_purge_score(const struct session *session);

void
session_clear_translate(struct session *session);

void
session_clear_user(struct session *session);

void
session_clear_language(struct session *session);

bool
session_set_translate(struct session *session, const char *translate);

bool
session_set_user(struct session *session, const char *user, unsigned max_age);

bool
session_set_language(struct session *session, const char *language);

/**
 * Finds the session with the specified id.  The returned object is
 * locked, and must be unlocked with session_put().
 */
struct session *
session_get(session_id_t id);

/**
 * Unlocks the specified session.
 */
void
session_put(struct session *session);

/**
 * Deletes the session with the specified id.  The current process
 * must not hold a sssion lock.
 */
void
session_delete(session_id_t id);

gcc_malloc
struct widget_session *
widget_session_allocate(struct session *session);

gcc_pure
struct widget_session *
session_get_widget(struct session *session, const char *id, bool create);

gcc_pure
struct widget_session *
widget_session_get_child(struct widget_session *parent, const char *id,
                         bool create);

void
widget_session_delete(struct dpool *pool, struct widget_session *ws);

void
session_delete_widgets(struct session *session);

#endif
