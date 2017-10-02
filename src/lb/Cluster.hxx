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

#ifndef BENG_LB_CLUSTER_HXX
#define BENG_LB_CLUSTER_HXX

#include "StickyHash.hxx"
#include "avahi/ExplorerListener.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/FailureRef.hxx"
#include "io/Logger.hxx"
#include "util/LeakDetector.hxx"

#include <boost/intrusive/set.hpp>

#include <vector>
#include <string>
#include <memory>

struct LbConfig;
struct LbGotoIfConfig;
struct LbListenerConfig;
struct LbClusterConfig;
class LbMonitorMap;
class FailureManager;
class MyAvahiClient;
class StickyCache;
class AvahiServiceExplorer;

class LbCluster final : AvahiServiceExplorerListener {
    const LbClusterConfig &config;
    FailureManager &failure_manager;

    const Logger logger;

    std::unique_ptr<AvahiServiceExplorer> explorer;

    class StickyRing;
    StickyRing *sticky_ring = nullptr;

    StickyCache *sticky_cache = nullptr;

    class Member
        : LeakDetector,
          public boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

        const std::string key;

        AllocatedSocketAddress address;

        FailureRef failure;

        mutable std::string log_name;

        unsigned refs = 1;

    public:
        Member(const std::string &_key, SocketAddress _address,
               ReferencedFailureInfo &_failure)
            :key(_key), address(_address), failure(_failure) {}

        Member(const Member &) = delete;
        Member &operator=(const Member &) = delete;

        void Ref() noexcept {
            ++refs;
        }

        void Unref() noexcept {
            if (--refs == 0)
                delete this;
        }

        const std::string &GetKey() const {
            return key;
        }

        SocketAddress GetAddress() const {
            return address;
        }

        void SetAddress(SocketAddress _address) {
            address = _address;
        }

        FailureInfo &GetFailureInfo() {
            return *failure;
        }

        /**
         * Obtain a name identifying this object for logging.
         */
        gcc_pure
        const char *GetLogName() const noexcept;

        struct Compare {
            bool operator()(const Member &a, const Member &b) const {
                return a.key < b.key;
            }

            bool operator()(const Member &a, const std::string &b) const {
                return a.key < b;
            }

            bool operator()(const std::string &a, const Member &b) const {
                return a < b.key;
            }
        };

        struct UnrefDisposer {
            void operator()(Member *member) const noexcept {
                member->Unref();
            }
        };
    };

public:
    /**
     * A (counted) reference to a #Member.  It keeps the #Member valid
     * even if it gets removed because the Zeroconf entry disappears.
     */
    class MemberPtr {
        Member *value = nullptr;

    public:
        MemberPtr() = default;

        MemberPtr(Member *_value) noexcept
            :value(_value) {
            if (value != nullptr)
                value->Ref();
        }

        MemberPtr(Member &_value) noexcept
            :value(&_value) {
            value->Ref();
        }

        MemberPtr(const MemberPtr &src) noexcept
            :MemberPtr(src.value) {}

        MemberPtr(MemberPtr &&src) noexcept
            :value(std::exchange(src.value, nullptr)) {}

        ~MemberPtr() {
            if (value != nullptr)
                value->Unref();
        }

        MemberPtr &operator=(const MemberPtr &src) noexcept {
            if (value != src.value) {
                if (value != nullptr)
                    value->Unref();

                value = src.value;

                if (value != nullptr)
                    value->Ref();
            }

            return *this;
        }

        MemberPtr &operator=(MemberPtr &&src) noexcept {
            std::swap(value, src.value);
            return *this;
        }

        Member *operator->() {
            return value;
        }

        Member &operator*() {
            return *value;
        }

        operator bool() const {
            return value != nullptr;
        }
    };

private:
    typedef boost::intrusive::set<Member,
                                  boost::intrusive::compare<Member::Compare>,
                                  boost::intrusive::constant_time_size<false>> MemberMap;
    MemberMap members;

    std::vector<MemberMap::pointer> active_members;

    bool dirty = false;

    unsigned last_pick = 0;

public:
    LbCluster(const LbClusterConfig &_config, FailureManager &_failure_manager,
              MyAvahiClient &avahi_client);
    ~LbCluster();

    const LbClusterConfig &GetConfig() const {
        return config;
    }

    /**
     * Create monitors for "static" members.
     */
    void CreateMonitors(LbMonitorMap &monitor_map);

    gcc_pure
    size_t GetZeroconfCount() {
        if (dirty) {
            dirty = false;
            FillActive();
        }

        return active_members.size();
    }

    Member *Pick(sticky_hash_t sticky_hash);

private:
    void FillActive();

    /**
     * Pick the next active Zeroconf member in a round-robin way.
     * Does not update the #StickyCache.
     */
    MemberMap::reference PickNextZeroconf();

    /**
     * Like PickNextZeroconf(), but skips members which are bad
     * according to failure_get_status().  If all are bad, a random
     * (bad) one is returned.
     */
    MemberMap::reference PickNextGoodZeroconf();

    /* virtual methods from class AvahiServiceExplorerListener */
    void OnAvahiNewObject(const std::string &key,
                          SocketAddress address) override;
    void OnAvahiRemoveObject(const std::string &key) override;
};

#endif
