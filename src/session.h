/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SESSION_H
#define __BENG_SESSION_H

#include "pool.h"
#include "list.h"
#include "strmap.h"
#include "hashmap.h"

#include <time.h>

struct widget;

typedef struct session *session_t;

typedef unsigned session_id_t;

/**
 * Session data associated with a widget instance (struct widget).
 */
struct widget_session {
    struct widget_session *parent;
    pool_t pool;

    /** local id of this widget; must not be NULL since widgets
        without an id cannot have a session */
    const char *id;
    hashmap_t children;

    /** last relative URI */
    const char *path_info;

    /** last query string */
    const char *query_string;

    /* XXX move cookies to struct widget_server_session */
    struct list_head cookies;
};

/**
 * A session associated with a user.
 */
struct session {
    struct list_head hash_siblings;
    pool_t pool;

    /** identification number of this session */
    session_id_t id;

    /** when will this session expire? */
    time_t expires;

    /** is this session removed from session_manager? */
    unsigned removed:1;

    /** an opaque string for the translation server */
    const char *translate;

    /** the user name which is logged in (NULL if anonymous), provided
        by the translation server */
    const char *user;

    /** a map of widget path to struct widget_session */
    hashmap_t widgets;
};

void
session_manager_init(pool_t pool);

void
session_manager_deinit(void);

session_id_t
session_id_parse(const char *p);

void
session_id_format(char dest[9], session_id_t id);

session_t
session_new(void);

session_t
session_get(session_id_t id);

void
session_remove(session_t session);

struct widget_session *
session_get_widget(session_t session, const char *id, int create);

struct widget_session *
widget_session_get_child(struct widget_session *parent, const char *id,
                         int create);

#endif
