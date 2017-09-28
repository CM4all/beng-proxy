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
#include "io/Logger.hxx"

#include <map>
#include <vector>
#include <string>
#include <memory>

struct LbConfig;
struct LbGotoIfConfig;
struct LbListenerConfig;
struct LbClusterConfig;
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

    class Member {
        const std::string key;

        AllocatedSocketAddress address;

        mutable std::string log_name;

    public:
        Member(const std::string &_key, SocketAddress _address)
            :key(_key), address(_address) {}

        Member(const Member &) = delete;
        Member &operator=(const Member &) = delete;

        SocketAddress GetAddress() const {
            return address;
        }

        void SetAddress(SocketAddress _address) {
            address = _address;
        }

        /**
         * Obtain a name identifying this object for logging.
         */
        gcc_pure
        const char *GetLogName() const noexcept;
    };

    typedef std::map<std::string, Member> MemberMap;
    MemberMap members;

    std::vector<MemberMap::const_pointer> active_members;

    bool dirty = false;

    unsigned last_pick = 0;

public:
    LbCluster(const LbClusterConfig &_config, FailureManager &_failure_manager,
              MyAvahiClient &avahi_client);
    ~LbCluster();

    const LbClusterConfig &GetConfig() const {
        return config;
    }

    gcc_pure
    size_t GetZeroconfCount() {
        if (dirty) {
            dirty = false;
            FillActive();
        }

        return active_members.size();
    }

    std::pair<const char *, SocketAddress> Pick(sticky_hash_t sticky_hash);

private:
    void FillActive();

    /**
     * Pick the next active Zeroconf member in a round-robin way.
     * Does not update the #StickyCache.
     */
    MemberMap::const_reference PickNextZeroconf();

    /**
     * Like PickNextZeroconf(), but skips members which are bad
     * according to failure_get_status().  If all are bad, a random
     * (bad) one is returned.
     */
    MemberMap::const_reference PickNextGoodZeroconf();

    /* virtual methods from class AvahiServiceExplorerListener */
    void OnAvahiNewObject(const std::string &key,
                          SocketAddress address) override;
    void OnAvahiRemoveObject(const std::string &key) override;
};

#endif
