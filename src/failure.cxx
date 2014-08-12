/*
 * Remember which servers (socket addresses) failed recently.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "failure.hxx"
#include "expiry.h"
#include "address_envelope.hxx"
#include "pool.hxx"
#include "util/djbhash.h"

#include <daemon/log.h>

#include <assert.h>
#include <string.h>
#include <time.h>
#include <errno.h>

struct Failure {
    Failure *next;

    time_t expires;

    time_t fade_expires;

    enum failure_status status;

    struct address_envelope envelope;

    bool CanExpire() const {
        return status != FAILURE_MONITOR;
    }

    gcc_pure
    bool IsExpired() const {
        return CanExpire() && is_expired(expires);
    }

    gcc_pure
    bool IsFade() const {
        return fade_expires > 0 && !is_expired(fade_expires);
    }

    enum failure_status GetStatus() const {
        if (!IsExpired())
            return status;
        else if (IsFade())
            return FAILURE_FADE;
        else
            return FAILURE_OK;
    }

    bool OverrideStatus(time_t now, enum failure_status new_status,
                        unsigned duration);
};

#define FAILURE_SLOTS 64

struct FailureList {
    struct pool *pool;

    Failure *slots[FAILURE_SLOTS];
};

static FailureList fl;

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

bool
Failure::OverrideStatus(time_t now, enum failure_status new_status,
                        unsigned duration)
{
    if (IsExpired()) {
        /* expired: override in any case */
    } else if (new_status == status) {
        /* same status: update expiry */
    } else if (new_status == FAILURE_FADE) {
        /* store "fade" expiry in special attribute, until the other
           failure status expires */
        fade_expires = now + duration;
        return true;
    } else if (status == FAILURE_FADE) {
        /* copy the "fade" expiry to the special attribute, and
           overwrite the FAILURE_FADE status */
        fade_expires = expires;
    } else if (new_status < status)
        return false;

    expires = now + duration;
    status = new_status;
    return true;
}

void
failure_set(const struct sockaddr *addr, size_t addrlen,
            enum failure_status status, unsigned duration)
{
    assert(addr != nullptr);
    assert(status > FAILURE_OK);

    const unsigned now = now_s();

    const unsigned slot = djb_hash(addr, addrlen) % FAILURE_SLOTS;
    for (Failure *failure = fl.slots[slot]; failure != nullptr;
         failure = failure->next) {
        if (failure->envelope.length == addrlen &&
            memcmp(&failure->envelope.address, addr, addrlen) == 0) {
            failure->OverrideStatus(now, status, duration);
            return;
        }
    }

    /* insert new failure object into the linked list */

    Failure *failure = (Failure *)
        p_malloc(fl.pool, sizeof(*failure)
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
failure_unset2(struct pool *pool, Failure **failure_r,
               Failure *failure, enum failure_status status)
{
    if (status == FAILURE_FADE)
        failure->fade_expires = 0;

    if (!match_status(failure->status, status) &&
        !failure->IsExpired())
        /* don't update if the current status is more serious than the
           one to be removed */
        return;

    if (status != FAILURE_OK && failure->IsFade()) {
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
    Failure **failure_r, *failure;

    assert(addr != nullptr);

    for (failure_r = &fl.slots[slot], failure = *failure_r;
         failure != nullptr;
         failure_r = &failure->next, failure = *failure_r) {
        if (failure->envelope.length == addrlen &&
            memcmp(&failure->envelope.address, addr, addrlen) == 0) {
            /* found it: remove it */
            failure_unset2(fl.pool, failure_r, failure, status);
            return;
        }
    }
}

enum failure_status
failure_get_status(const struct sockaddr *address, size_t length)
{
    unsigned slot = djb_hash(address, length) % FAILURE_SLOTS;
    Failure *failure;

    assert(address != nullptr);
    assert(length >= sizeof(failure->envelope.address));

    for (failure = fl.slots[slot]; failure != nullptr; failure = failure->next)
        if (failure->envelope.length == length &&
            memcmp(&failure->envelope.address, address, length) == 0)
            return failure->GetStatus();

    return FAILURE_OK;
}
