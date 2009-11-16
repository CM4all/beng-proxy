/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SESSION_H
#define __BENG_SESSION_H

#include "lock.h"

#include <inline/list.h>
#include <inline/compiler.h>

#include <time.h>
#include <stdbool.h>
#include <stdint.h>

struct dpool;
struct dhashmap;

typedef uint64_t session_id_t;

/**
 * Buffer for the function session_id_format().
 */
struct session_id_string {
    /**
     * Two hex characters per byte, plus the terminating zero.
     */
    char buffer[sizeof(session_id_t) * 2 + 1];
};

/**
 * Session data associated with a widget instance (struct widget).
 */
struct widget_session {
    struct widget_session *parent;

    struct session *session;

    /** local id of this widget; must not be NULL since widgets
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
    bool new;

    /** has a HTTP cookie with this session id already been sent? */
    bool cookie_sent;

    /** has a HTTP cookie with this session id already been received? */
    bool cookie_received;

    /** an opaque string for the translation server */
    const char *translate;

    /** the user name which is logged in (NULL if anonymous), provided
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

/**
 * Initialize the global session manager or increase the reference
 * counter.
 */
bool
session_manager_init(void);

/**
 * Decrease the reference counter and destroy the global session
 * manager if it has become zero.
 */
void
session_manager_deinit(void);

/**
 * Release the session manager and try not to access the shared
 * memory, because we assume it may be corrupted.
 */
void
session_manager_abandon(void);

/**
 * Re-add all libevent events after session_manager_event_del().
 */
void
session_manager_event_add(void);

/**
 * Removes all libevent events.  Call this before fork(), or before
 * creating a new event base.  Don't forget to call
 * session_manager_event_add() afterwards.
 */
void
session_manager_event_del(void);

static inline bool
session_id_is_defined(session_id_t id)
{
    return id != 0;
}

static inline void
session_id_clear(session_id_t *id_p)
{
    *id_p = 0;
}

/**
 * Parse a session id from a string.
 *
 * @return true on success, false on error
 */
bool
session_id_parse(const char *p, session_id_t *id_r);

const char *
session_id_format(session_id_t id, struct session_id_string *string);

/**
 * Create a new session with a random session id.
 *
 * The returned session object is locked and must be unlocked with
 * session_put().
 */
struct session * __attr_malloc
session_new(void);

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

struct widget_session *
session_get_widget(struct session *session, const char *id, bool create);

struct widget_session *
widget_session_get_child(struct widget_session *parent, const char *id,
                         bool create);

#endif
