/*
 * Session management.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_SESSION_H
#define __BENG_SESSION_H

#include "pool.h"
#include "list.h"

#include <time.h>

typedef struct session *session_t;

typedef unsigned session_id_t;

struct session {
    struct list_head hash_siblings;
    pool_t pool;
    session_id_t id;
    time_t expires;
    unsigned removed:1;
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

#endif
