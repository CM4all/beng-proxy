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

#include "FailureRef.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/LeakDetector.hxx"
#include "util/Compiler.h"

#include <boost/intrusive/unordered_set.hpp>

#include <chrono>

/*
 * Remember which servers (socket addresses) failed recently.
 */
class FailureManager {

    class Failure final
        : public boost::intrusive::unordered_set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>>,
          LeakDetector,
          public ReferencedFailureInfo {

        const AllocatedSocketAddress address;

    public:
        explicit Failure(SocketAddress _address) noexcept
            :address(_address) {}

        SocketAddress GetAddress() const noexcept {
            return address;
        }

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

    protected:
        void Destroy() override {
            delete this;
        }
    };

    typedef boost::intrusive::unordered_set<Failure,
                                            boost::intrusive::hash<Failure::Hash>,
                                            boost::intrusive::equal<Failure::Equal>,
                                            boost::intrusive::constant_time_size<false>> FailureSet;

    static constexpr size_t N_BUCKETS = 97;

    FailureSet::bucket_type buckets[N_BUCKETS];

    FailureSet failures;

public:
    FailureManager() noexcept
        :failures(FailureSet::bucket_traits(buckets, N_BUCKETS)) {}

    ~FailureManager() noexcept;

    FailureManager(const FailureManager &) = delete;
    FailureManager &operator=(const FailureManager &) = delete;

    /**
     * Looks up a #FailureInfo instance or creates a new one.  The
     * return value should be passed to the #FailureRef constructor.
     */
    ReferencedFailureInfo &Make(SocketAddress address) noexcept;

    SocketAddress GetAddress(const FailureInfo &info) const noexcept {
        const auto &f = (const Failure &)info;
        return f.GetAddress();
    }

    void Unset(Expiry now, SocketAddress address,
               enum failure_status status) noexcept;

    gcc_pure
    enum failure_status Get(Expiry now, SocketAddress address) const noexcept;

    gcc_pure
    bool Check(Expiry now, SocketAddress address,
               bool allow_fade=false) const noexcept;
};

#endif
