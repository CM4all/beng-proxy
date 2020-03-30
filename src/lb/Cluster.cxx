/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "MonitorStock.hxx"
#include "MonitorRef.hxx"
#include "fs/Balancer.hxx"
#include "cluster/StickyCache.hxx"
#include "sodium/GenericHash.hxx"
#include "system/Error.hxx"
#include "net/FailureManager.hxx"
#include "net/ToString.hxx"
#include "util/HashRing.hxx"
#include "util/ConstBuffer.hxx"
#include "AllocatorPtr.hxx"

#ifdef HAVE_AVAHI
#include "avahi/Explorer.hxx"

#include <net/if.h>
#endif

#ifdef HAVE_AVAHI

class LbCluster::StickyRing final : public HashRing<MemberMap::pointer,
						    sticky_hash_t,
						    4096, 8> {};

LbCluster::Member::Member(const std::string &_key, SocketAddress _address,
			  ReferencedFailureInfo &_failure,
			  LbMonitorStock *monitors)
	:key(_key), address(_address), failure(_failure),
	 monitor(monitors != nullptr
		 ? std::make_unique<LbMonitorRef>(monitors->Add(key.c_str(),
								_address))
		 : std::unique_ptr<LbMonitorRef>())
{
}

LbCluster::Member::~Member() noexcept = default;

const char *
LbCluster::Member::GetLogName() const noexcept
{
	if (log_name.empty()) {
		if (address.IsNull())
			return key.c_str();

		log_name = key.c_str();

		char buffer[512];
		if (ToString(buffer, sizeof(buffer), address)) {
			log_name += " (";
			log_name += buffer;
			log_name += ")";
		}
	}

	return log_name.c_str();
}

#endif

LbCluster::LbCluster(const LbClusterConfig &_config,
		     FailureManager &_failure_manager,
		     FilteredSocketBalancer &_fs_balancer,
		     LbMonitorStock *_monitors
#ifdef HAVE_AVAHI
		     , MyAvahiClient &avahi_client
#endif
		     )
	:config(_config), failure_manager(_failure_manager),
	 fs_balancer(_fs_balancer),
	 monitors(_monitors),
	 logger("cluster " + config.name)
{
#ifdef HAVE_AVAHI
	if (config.HasZeroConf()) {
		AvahiIfIndex interface = AVAHI_IF_UNSPEC;

		if (!config.zeroconf_interface.empty()) {
			int i = if_nametoindex(config.zeroconf_interface.c_str());
			if (i == 0)
				throw FormatErrno("Failed to find interface '%s'",
						  config.zeroconf_interface.c_str());

			interface = AvahiIfIndex(i);
		}

		explorer.reset(new AvahiServiceExplorer(avahi_client, *this,
							interface,
							AVAHI_PROTO_UNSPEC,
							config.zeroconf_service.c_str(),
							config.zeroconf_domain.empty()
							? nullptr
							: config.zeroconf_domain.c_str()));
	}
#endif

	if (monitors != nullptr)
		/* create monitors for "static" members */
		for (const auto &member : config.members)
			static_member_monitors.emplace_front(monitors->Add(*member.node,
									   member.port));
}

LbCluster::~LbCluster() noexcept
{
#ifdef HAVE_AVAHI
	members.clear_and_dispose(Member::UnrefDisposer());
#endif
}

void
LbCluster::ConnectStaticHttp(AllocatorPtr alloc,
			     const StopwatchPtr &parent_stopwatch,
			     SocketAddress bind_address,
			     sticky_hash_t session_sticky,
			     Event::Duration timeout,
			     SocketFilterFactory *filter_factory,
			     StockGetHandler &handler,
			     CancellablePointer &cancel_ptr) noexcept
{
	fs_balancer.Get(alloc, parent_stopwatch,
			config.transparent_source,
			bind_address,
			session_sticky,
			config.address_list,
			timeout,
			filter_factory,
			handler, cancel_ptr);
}

#ifdef HAVE_AVAHI

LbCluster::MemberMap::reference
LbCluster::PickNextZeroconf() noexcept
{
	assert(!active_members.empty());

	++last_pick;
	if (last_pick >= active_members.size())
		last_pick = 0;

	return *active_members[last_pick];
}

LbCluster::MemberMap::reference
LbCluster::PickNextGoodZeroconf(const Expiry now) noexcept
{
	assert(!active_members.empty());

	unsigned remaining = active_members.size();

	while (true) {
		auto &m = PickNextZeroconf();
		if (--remaining == 0 ||
		    m.GetFailureInfo().Check(now))
			return m;
	}
}

LbCluster::Member *
LbCluster::Pick(const Expiry now, sticky_hash_t sticky_hash) noexcept
{
	if (dirty) {
		dirty = false;
		FillActive();
	}

	if (active_members.empty())
		return nullptr;

	if (sticky_hash != 0 && config.sticky_cache) {
		/* look up the sticky_hash in the StickyCache */

		assert(config.sticky_mode != StickyMode::NONE);

		if (sticky_cache == nullptr)
			/* lazy cache allocation */
			sticky_cache = std::make_unique<StickyCache>();

		const auto *cached = sticky_cache->Get(sticky_hash);
		if (cached != nullptr) {
			/* cache hit */
			auto i = members.find(*cached, members.key_comp());
			if (i != members.end() &&
			    // TODO: allow FAILURE_FADE here?
			    i->GetFailureInfo().Check(now))
				/* the node is active, we can use it */
				return &*i;

			sticky_cache->Remove(sticky_hash);
		}

		/* cache miss or cached node not active: fall back to
		   round-robin and remember the new pick in the cache */
	} else if (sticky_hash != 0) {
		/* use consistent hashing */

		assert(sticky_ring != nullptr);

		auto *i = sticky_ring->Pick(sticky_hash);
		assert(i != nullptr);

		unsigned retries = active_members.size();
		while (true) {
			if (--retries == 0 ||
			    i->GetFailureInfo().Check(now))
				return &*i;

			/* the node is known-bad; pick the next one in the ring */
			const auto next = sticky_ring->FindNext(sticky_hash);
			sticky_hash = next.first;
			i = next.second;
		}
	}

	auto &i = PickNextGoodZeroconf(now);

	if (sticky_hash != 0)
		sticky_cache->Put(sticky_hash, i.GetKey());

	return &i;
}

void
LbCluster::FillActive() noexcept
{
	active_members.clear();
	active_members.reserve(members.size());

	for (auto &i : members)
		active_members.push_back(&i);

	if (!config.sticky_cache) {
		if (sticky_ring == nullptr)
			/* lazy allocation */
			sticky_ring = std::make_unique<StickyRing>();

		/**
		 * Functor class which generates a #HashRing hash for a
		 * cluster member combined with a replica number.
		 */
		struct MemberHasher {
			gcc_pure
			sticky_hash_t operator()(MemberMap::const_pointer member,
						 size_t replica) const {
				/* use libsodium's "generichash" (BLAKE2b) which is
				   secure enough for class HashRing */
				union {
					unsigned char hash[crypto_generichash_BYTES];
					sticky_hash_t result;
				} u;

				GenericHashState state(sizeof(u.hash));
				state.Update(member->GetAddress().GetSteadyPart());
				state.UpdateT(replica);
				state.Final(u.hash, sizeof(u.hash));

				return u.result;
			}
		};

		sticky_ring->Build(active_members, MemberHasher());
	}
}

void
LbCluster::OnAvahiNewObject(const std::string &key,
			    SocketAddress address) noexcept
{
	MemberMap::insert_commit_data hint;
	auto result = members.insert_check(key, members.key_comp(), hint);
	if (result.second) {
		auto *member = new Member(key, address, failure_manager.Make(address),
					  monitors);
		members.insert_commit(*member, hint);
	} else {
		/* update existing member */
		result.first->SetAddress(address);
	}

	dirty = true;
}

void
LbCluster::OnAvahiRemoveObject(const std::string &key) noexcept
{
	auto i = members.find(key, members.key_comp());
	if (i == members.end())
		return;

	/* TODO: purge entry from the "failure" map, because it
	   will never be used again anyway */

	members.erase_and_dispose(i, Member::UnrefDisposer());
	dirty = true;
}

#endif
