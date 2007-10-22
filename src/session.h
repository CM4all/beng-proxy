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

struct cookie {
    const char *name, *value;
    time_t valid_until;
};

struct widget_session {
    struct widget_session *parent;
    pool_t pool;
    const char *id;
    hashmap_t children;
    const char *query_string;
};

struct client_session {
    const char *server;
    strmap_t cookies;
};

struct session {
    struct list_head hash_siblings;
    pool_t pool;
    session_id_t id;
    time_t expires;
    unsigned removed:1;

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
