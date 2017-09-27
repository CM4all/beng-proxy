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

#ifndef FAILURE_MANAGER_HXX
#define FAILURE_MANAGER_HXX

#include "net/AllocatedSocketAddress.hxx"
#include "util/Compiler.h"
#include "util/Expiry.hxx"

#include <boost/intrusive/unordered_set.hpp>

#include <chrono>

enum failure_status {
    /**
     * No failure, host is ok.
     */
    FAILURE_OK,

    /**
     * Host is being faded out (graceful shutdown).  No new sessions.
     */
    FAILURE_FADE,

    /**
     * The response received from the server indicates a server error.
     */
    FAILURE_RESPONSE,

    /**
     * Failed to connect to the host.
     */
    FAILURE_CONNECT,

    /**
     * The failure was submitted by a "monitor", and will not expire
     * until the monitor detects recovery.
     */
    FAILURE_MONITOR,
};

/*
 * Remember which servers (socket addresses) failed recently.
 */
class FailureManager {

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
            size_t operator()(const SocketAddress a) const noexcept;

            gcc_pure
            size_t operator()(const Failure &f) const noexcept {
                return this->operator()(f.address);
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

    FailureSet::bucket_type failure_buckets[N_FAILURE_BUCKETS];

    FailureSet failures;

public:
    FailureManager() noexcept
        :failures(FailureSet::bucket_traits(failure_buckets,
                                            N_FAILURE_BUCKETS)) {}

    ~FailureManager() noexcept;

    FailureManager(const FailureManager &) = delete;
    FailureManager &operator=(const FailureManager &) = delete;

    void Set(SocketAddress address, enum failure_status status,
             std::chrono::seconds duration) noexcept;

    /**
     * Unset a failure status.
     *
     * @param status the status to be removed; #FAILURE_OK is a catch-all
     * status that matches everything
     */
    void Unset(SocketAddress address, enum failure_status status) noexcept;

    gcc_pure
    enum failure_status Get(SocketAddress address) noexcept;

private:
    void Unset(Failure &failure, enum failure_status status) noexcept;
};

#endif
