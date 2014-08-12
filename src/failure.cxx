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

#include <boost/intrusive/unordered_set.hpp>

#include <assert.h>
#include <time.h>

struct Failure
    : boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    const AllocatedSocketAddress address;

    time_t expires;

    time_t fade_expires;

    enum failure_status status;

    Failure(SocketAddress _address, enum failure_status _status,
            time_t _expires)
        :address(_address),
         expires(_expires), fade_expires(0),
         status(_status) {}

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

    struct Hash {
        gcc_pure
        size_t operator()(const SocketAddress a) const {
            assert(!a.IsNull());

            return djb_hash(a.GetAddress(), a.GetSize());
        }

        gcc_pure
        size_t operator()(const Failure &f) const {
            return djb_hash(f.address.GetAddress(), f.address.GetSize());
        }
    };

    struct Equal {
        gcc_pure
        bool operator()(const SocketAddress a, const SocketAddress b) const {
            return a == b;
        }

        gcc_pure
        bool operator()(const SocketAddress a, const Failure &b) const {
            return a == b.address;
        }
    };

    struct Disposer {
        void operator()(Failure *f) {
            delete f;
        }
    };
};

typedef boost::intrusive::unordered_set<Failure,
                                        boost::intrusive::hash<Failure::Hash>,
                                        boost::intrusive::equal<Failure::Equal>,
                                        boost::intrusive::constant_time_size<false>> FailureSet;

static constexpr size_t N_FAILURE_BUCKETS = 97;

static FailureSet::bucket_type failure_buckets[N_FAILURE_BUCKETS];

static FailureSet failures(FailureSet::bucket_traits(failure_buckets,
                                                     N_FAILURE_BUCKETS));

void
failure_init()
{
}

void
failure_deinit(void)
{
    failures.clear_and_dispose(Failure::Disposer());
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
failure_set(SocketAddress address,
            enum failure_status status, unsigned duration)
{
    assert(!address.IsNull());
    assert(status > FAILURE_OK);

    const unsigned now = now_s();

    FailureSet::insert_commit_data hint;
    auto result = failures.insert_check(address, Failure::Hash(),
                                        Failure::Equal(), hint);
    if (result.second) {
        Failure *failure = new Failure(address, status, now + duration);
        failures.insert_commit(*failure, hint);
    } else {
        Failure &failure = *result.first;
        failure.OverrideStatus(now, status, duration);
    }
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
failure_unset2(Failure &failure, enum failure_status status)
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
        failures.erase_and_dispose(failures.iterator_to(failure),
                                   Failure::Disposer());
    }
}

void
failure_unset(SocketAddress address, enum failure_status status)
{
    assert(!address.IsNull());

    auto i = failures.find(address, Failure::Hash(), Failure::Equal());
    if (i != failures.end())
        failure_unset2(*i, status);
}

enum failure_status
failure_get_status(SocketAddress address)
{
    assert(!address.IsNull());

    auto i = failures.find(address, Failure::Hash(), Failure::Equal());
    if (i == failures.end())
        return FAILURE_OK;

    return i->GetStatus();
}
