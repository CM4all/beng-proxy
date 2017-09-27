/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "failure.hxx"
#include "net/SocketAddress.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/djbhash.h"
#include "util/DeleteDisposer.hxx"
#include "util/Expiry.hxx"

#include <boost/intrusive/unordered_set.hpp>

#include <assert.h>

struct Failure
    : boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

    const AllocatedSocketAddress address;

    Expiry expires;

    Expiry fade_expires = Expiry::AlreadyExpired();

    enum failure_status status;

    Failure(SocketAddress _address, enum failure_status _status,
            Expiry _expires) noexcept
        :address(_address),
         expires(_expires),
         status(_status) {}

    bool CanExpire() const noexcept {
        return status != FAILURE_MONITOR;
    }

    gcc_pure
    bool IsExpired() const noexcept {
        return CanExpire() && expires.IsExpired();
    }

    gcc_pure
    bool IsFade() const noexcept {
        return !fade_expires.IsExpired();
    }

    enum failure_status GetStatus() const noexcept {
        if (!IsExpired())
            return status;
        else if (IsFade())
            return FAILURE_FADE;
        else
            return FAILURE_OK;
    }

    bool OverrideStatus(Expiry now, enum failure_status new_status,
                        std::chrono::seconds duration) noexcept;

    struct Hash {
        gcc_pure
        size_t operator()(const SocketAddress a) const noexcept {
            assert(!a.IsNull());

            return djb_hash(a.GetAddress(), a.GetSize());
        }

        gcc_pure
        size_t operator()(const Failure &f) const noexcept {
            return djb_hash(f.address.GetAddress(), f.address.GetSize());
        }
    };

    struct Equal {
        gcc_pure
        bool operator()(const SocketAddress a,
                        const SocketAddress b) const noexcept {
            return a == b;
        }

        gcc_pure
        bool operator()(const SocketAddress a,
                        const Failure &b) const noexcept {
            return a == b.address;
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
failure_init() noexcept
{
}

void
failure_deinit() noexcept
{
    failures.clear_and_dispose(DeleteDisposer());
}

bool
Failure::OverrideStatus(Expiry now, enum failure_status new_status,
                        std::chrono::seconds duration) noexcept
{
    if (IsExpired()) {
        /* expired: override in any case */
    } else if (new_status == status) {
        /* same status: update expiry */
    } else if (new_status == FAILURE_FADE) {
        /* store "fade" expiry in special attribute, until the other
           failure status expires */
        fade_expires.Touch(now, duration);
        return true;
    } else if (status == FAILURE_FADE) {
        /* copy the "fade" expiry to the special attribute, and
           overwrite the FAILURE_FADE status */
        fade_expires = expires;
    } else if (new_status < status)
        return false;

    expires.Touch(now, duration);
    status = new_status;
    return true;
}

void
failure_set(SocketAddress address,
            enum failure_status status, std::chrono::seconds duration) noexcept
{
    assert(!address.IsNull());
    assert(status > FAILURE_OK);

    const Expiry now = Expiry::Now();

    FailureSet::insert_commit_data hint;
    auto result = failures.insert_check(address, Failure::Hash(),
                                        Failure::Equal(), hint);
    if (result.second) {
        Failure *failure = new Failure(address, status,
                                       Expiry::Touched(now, duration));
        failures.insert_commit(*failure, hint);
    } else {
        Failure &failure = *result.first;
        failure.OverrideStatus(now, status, duration);
    }
}

void
failure_add(SocketAddress address) noexcept
{
    failure_set(address, FAILURE_FAILED, std::chrono::seconds(20));
}

static constexpr bool
match_status(enum failure_status current, enum failure_status match) noexcept
{
    /* FAILURE_OK is a catch-all magic value */
    return match == FAILURE_OK || current == match;
}

static void
failure_unset2(Failure &failure, enum failure_status status) noexcept
{
    if (status == FAILURE_FADE)
        failure.fade_expires = Expiry::AlreadyExpired();

    if (!match_status(failure.status, status) && !failure.IsExpired())
        /* don't update if the current status is more serious than the
           one to be removed */
        return;

    if (status != FAILURE_OK && failure.IsFade()) {
        failure.status = FAILURE_FADE;
        failure.expires = failure.fade_expires;
        failure.fade_expires = Expiry::AlreadyExpired();
    } else {
        failures.erase_and_dispose(failures.iterator_to(failure),
                                   DeleteDisposer());
    }
}

void
failure_unset(SocketAddress address, enum failure_status status) noexcept
{
    assert(!address.IsNull());

    auto i = failures.find(address, Failure::Hash(), Failure::Equal());
    if (i != failures.end())
        failure_unset2(*i, status);
}

enum failure_status
failure_get_status(SocketAddress address) noexcept
{
    assert(!address.IsNull());

    auto i = failures.find(address, Failure::Hash(), Failure::Equal());
    if (i == failures.end())
        return FAILURE_OK;

    return i->GetStatus();
}
