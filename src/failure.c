/*
 * Remember which servers (socket addresses) failed recently.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "failure.h"
#include "expiry.h"
#include "address_envelope.h"
#include "djbhash.h"
#include "pool.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>
#include <time.h>
#include <errno.h>

struct failure {
    struct failure *next;

    time_t expires;

    time_t fade_expires;

    enum failure_status status;

    struct address_envelope envelope;
};

#define FAILURE_SLOTS 64

struct failure_list {
    struct pool *pool;

    struct failure *slots[FAILURE_SLOTS];
};

static struct failure_list fl;

void
failure_init(struct pool *pool)
{
    fl.pool = pool_new_libc(pool, "failure_list");
    memset(fl.slots, 0, sizeof(fl.slots));
}

void
failure_deinit(void)
{
    pool_unref(fl.pool);
}

gcc_const
static inline bool
failure_status_can_expire(enum failure_status status)
{
    return status != FAILURE_MONITOR;
}

gcc_pure
static inline bool
failure_is_expired(const struct failure *failure)
{
    assert(failure != NULL);

    return failure_status_can_expire(failure->status) &&
        is_expired(failure->expires);
}

gcc_pure
static inline bool
failure_is_fade(const struct failure *failure)
{
    assert(failure != NULL);

    return failure->fade_expires > 0 &&
        !is_expired(failure->fade_expires);
}

static bool
failure_override_status(struct failure *failure, time_t now,
                        enum failure_status status, unsigned duration)
{
    if (failure_is_expired(failure)) {
        /* expired: override in any case */
    } else if (status == failure->status) {
        /* same status: update expiry */
    } else if (status == FAILURE_FADE) {
        /* store "fade" expiry in special attribute, until the other
           failure status expires */
        failure->fade_expires = now + duration;
        return true;
    } else if (failure->status == FAILURE_FADE) {
        /* copy the "fade" expiry to the special attribute, and
           overwrite the FAILURE_FADE status */
        failure->fade_expires = failure->expires;
    } else if (status < failure->status)
        return false;

    failure->expires = now + duration;
    failure->status = status;
    return true;
}

void
failure_set(const struct sockaddr *addr, size_t addrlen,
            enum failure_status status, unsigned duration)
{
    unsigned slot = djb_hash(addr, addrlen) % FAILURE_SLOTS;
    struct failure *failure;

    assert(addr != NULL);
    assert(status > FAILURE_OK);

    const unsigned now = now_s();

    for (failure = fl.slots[slot]; failure != NULL; failure = failure->next) {
        if (failure->envelope.length == addrlen &&
            memcmp(&failure->envelope.address, addr, addrlen) == 0) {
            failure_override_status(failure, now, status, duration);
            return;
        }
    }

    /* insert new failure object into the linked list */

    failure = p_malloc(fl.pool, sizeof(*failure)
                       - sizeof(failure->envelope.address) + addrlen);
    failure->expires = now + duration;
    failure->fade_expires = 0;
    failure->status = status;
    failure->envelope.length = addrlen;
    memcpy(&failure->envelope.address, addr, addrlen);

    failure->next = fl.slots[slot];
    fl.slots[slot] = failure;
}

static bool
match_status(enum failure_status current, enum failure_status match)
{
    /* FAILURE_OK is a catch-all magic value */
    return match == FAILURE_OK || current == match;
}

static void
failure_unset2(struct pool *pool, struct failure **failure_r,
               struct failure *failure, enum failure_status status)
{
    if (status == FAILURE_FADE)
        failure->fade_expires = 0;

    if (!match_status(failure->status, status) &&
        !failure_is_expired(failure))
        /* don't update if the current status is more serious than the
           one to be removed */
        return;

    if (status != FAILURE_OK && failure_is_fade(failure)) {
        failure->status = FAILURE_FADE;
        failure->expires = failure->fade_expires;
        failure->fade_expires = 0;
    } else {
        *failure_r = failure->next;
        p_free(pool, failure);
    }
}

void
failure_unset(const struct sockaddr *addr, size_t addrlen,
              enum failure_status status)
{
    unsigned slot = djb_hash(addr, addrlen) % FAILURE_SLOTS;
    struct failure **failure_r, *failure;

    assert(addr != NULL);
    assert(addrlen >= sizeof(failure->envelope.address));

    for (failure_r = &fl.slots[slot], failure = *failure_r;
         failure != NULL;
         failure_r = &failure->next, failure = *failure_r) {
        if (failure->envelope.length == addrlen &&
            memcmp(&failure->envelope.address, addr, addrlen) == 0) {
            /* found it: remove it */
            failure_unset2(fl.pool, failure_r, failure, status);
            return;
        }
    }
}

gcc_pure
static enum failure_status
failure_get_status2(const struct failure *failure)
{
    if (!failure_is_expired(failure))
        return failure->status;
    else if (failure_is_fade(failure))
        return FAILURE_FADE;
    else
        return FAILURE_OK;
}

enum failure_status
failure_get_status(const struct sockaddr *address, size_t length)
{
    unsigned slot = djb_hash(address, length) % FAILURE_SLOTS;
    struct failure *failure;

    assert(address != NULL);
    assert(length >= sizeof(failure->envelope.address));

    for (failure = fl.slots[slot]; failure != NULL; failure = failure->next)
        if (failure->envelope.length == length &&
            memcmp(&failure->envelope.address, address, length) == 0)
            return failure_get_status2(failure);

    return FAILURE_OK;
}
