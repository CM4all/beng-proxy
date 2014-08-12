/*
 * Remember which servers (socket addresses) failed recently.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "failure.hxx"
#include "expiry.h"
#include "pool.hxx"
#include "net/SocketAddress.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/djbhash.h"

#include <daemon/log.h>

#include <assert.h>
#include <time.h>

struct Failure {
    Failure *next;

    const AllocatedSocketAddress address;

    time_t expires;

    time_t fade_expires;

    enum failure_status status;

    Failure(SocketAddress _address)
        :address(_address) {}

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
    Failure *slots[FAILURE_SLOTS];
};

static FailureList fl;

void
failure_init()
{
}

void
failure_deinit(void)
{
    for (auto &i : fl.slots) {
        while (i != nullptr) {
            Failure *failure = i;
            i = failure->next;

            delete failure;
        }
    }
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

gcc_pure
static unsigned
Hash(SocketAddress address)
{
    assert(!address.IsNull());

    return djb_hash(address.GetAddress(), address.GetSize());
}

gcc_pure
static bool
Compare(SocketAddress a, SocketAddress b)
{
    return a == b;
}

void
failure_set(SocketAddress address,
            enum failure_status status, unsigned duration)
{
    assert(!address.IsNull());
    assert(status > FAILURE_OK);

    const unsigned now = now_s();

    const unsigned slot = Hash(address) % FAILURE_SLOTS;
    for (Failure *failure = fl.slots[slot]; failure != nullptr;
         failure = failure->next) {
        if (Compare(failure->address, address)) {
            failure->OverrideStatus(now, status, duration);
            return;
        }
    }

    /* insert new failure object into the linked list */

    Failure *failure = new Failure(address);

    failure->expires = now + duration;
    failure->fade_expires = 0;
    failure->status = status;

    failure->next = fl.slots[slot];
    fl.slots[slot] = failure;
}

void
failure_add(SocketAddress address)
{
    failure_set(address, FAILURE_FAILED, 20);
}

static bool
match_status(enum failure_status current, enum failure_status match)
{
    /* FAILURE_OK is a catch-all magic value */
    return match == FAILURE_OK || current == match;
}

static void
failure_unset2(Failure **failure_r,
               Failure &failure, enum failure_status status)
{
    if (status == FAILURE_FADE)
        failure.fade_expires = 0;

    if (!match_status(failure.status, status) && !failure.IsExpired())
        /* don't update if the current status is more serious than the
           one to be removed */
        return;

    if (status != FAILURE_OK && failure.IsFade()) {
        failure.status = FAILURE_FADE;
        failure.expires = failure.fade_expires;
        failure.fade_expires = 0;
    } else {
        *failure_r = failure.next;
        delete &failure;
    }
}

void
failure_unset(SocketAddress address, enum failure_status status)
{
    assert(!address.IsNull());

    unsigned slot = Hash(address) % FAILURE_SLOTS;
    Failure **failure_r, *failure;

    for (failure_r = &fl.slots[slot], failure = *failure_r;
         failure != nullptr;
         failure_r = &failure->next, failure = *failure_r) {
        if (Compare(failure->address, address)) {
            /* found it: remove it */
            failure_unset2(failure_r, *failure, status);
            return;
        }
    }
}

enum failure_status
failure_get_status(SocketAddress address)
{
    assert(!address.IsNull());

    unsigned slot = Hash(address) % FAILURE_SLOTS;
    Failure *failure;

    assert(address != nullptr);

    for (failure = fl.slots[slot]; failure != nullptr; failure = failure->next)
        if (Compare(failure->address, address))
            return failure->GetStatus();

    return FAILURE_OK;
}
