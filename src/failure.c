/*
 * Remember which servers (socket addresses) failed recently.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "failure.h"

#include <assert.h>
#include <string.h>
#include <time.h>

struct failure {
    struct failure *next;

    time_t expires;

    socklen_t addrlen;
    struct sockaddr addr;
};

#define FAILURE_SLOTS 64

struct failure_list {
    pool_t pool;

    struct failure *slots[FAILURE_SLOTS];
};

static struct failure_list fl;

static inline unsigned
calc_hash(const struct sockaddr *addr, socklen_t addrlen)
{
    const char *p = (const char*)addr;
    unsigned hash = 5381;

    assert(p != NULL);

    while (addrlen-- > 0)
        hash = (hash << 5) + hash + *p++;

    return hash;
}

void
failure_init(pool_t pool)
{
    fl.pool = pool_new_libc(pool, "failure_list");
    memset(fl.slots, 0, sizeof(fl.slots));
}

void
failure_deinit(void)
{
    pool_unref(fl.pool);
}

void
failure_add(const struct sockaddr *addr, socklen_t addrlen)
{
    unsigned slot = calc_hash(addr, addrlen) % FAILURE_SLOTS;
    struct failure *failure;

    assert(addr != NULL);
    assert(addrlen >= sizeof(failure->addr));

    for (failure = fl.slots[slot]; failure != NULL; failure = failure->next) {
        if (failure->addrlen == addrlen &&
            memcmp(&failure->addr, addr, addrlen) == 0)
            /* this address is already in our list */
            return;
    }

    /* insert new failure object into the linked list */

    failure = p_malloc(fl.pool,
                       sizeof(*failure) - sizeof(failure->addr) + addrlen);
    failure->expires = time(NULL) + 20;
    failure->addrlen = addrlen;
    memcpy(&failure->addr, addr, addrlen);

    failure->next = fl.slots[slot];
    fl.slots[slot] = failure;
}

void
failure_remove(const struct sockaddr *addr, socklen_t addrlen)
{
    unsigned slot = calc_hash(addr, addrlen) % FAILURE_SLOTS;
    struct failure **failure_r, *failure;

    assert(addr != NULL);
    assert(addrlen >= sizeof(failure->addr));

    for (failure_r = &fl.slots[slot], failure = *failure_r;
         failure != NULL;
         failure_r = &failure->next, failure = *failure_r) {
        if (failure->addrlen == addrlen &&
            memcmp(&failure->addr, addr, addrlen) == 0) {
            /* found it: remove it */

            *failure_r = failure->next;
            p_free(fl.pool, failure);
            return;
        }
    }
}

bool
failure_check(const struct sockaddr *addr, socklen_t addrlen)
{
    unsigned slot = calc_hash(addr, addrlen) % FAILURE_SLOTS;
    struct failure *failure;

    assert(addr != NULL);
    assert(addrlen >= sizeof(failure->addr));

    for (failure = fl.slots[slot]; failure != NULL; failure = failure->next)
        if (failure->addrlen == addrlen &&
            memcmp(&failure->addr, addr, addrlen) == 0)
            return time(NULL) >= failure->expires;

    return false;
}
