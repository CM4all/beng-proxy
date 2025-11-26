// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "cluster/StickyHash.hxx"
#include "cluster/RoundRobinBalancer.hxx"
#include "event/Chrono.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/FailureRef.hxx"
#include "io/Logger.hxx"
#include "config.h"

#ifdef HAVE_AVAHI
#include "lib/avahi/ExplorerListener.hxx"
#endif

#include <cstdint>
#include <forward_list>
#include <vector>
#include <string>
#include <map>
#include <memory>

enum class Arch : uint_least8_t;
struct LbClusterConfig;
struct LbContext;
class LbMonitorStock;
class LbMonitorRef;
class FailureManager;
class BalancerMap;
class FilteredSocketStock;
class FilteredSocketBalancer;
class StickyCache;
namespace Avahi { class ServiceExplorer; }
class StopwatchPtr;
class SslSocketFilterParams;
class FilteredSocketBalancerHandler;
class ConnectSocketHandler;
class CancellablePointer;
class AllocatorPtr;

class LbCluster final
#ifdef HAVE_AVAHI
	: Avahi::ServiceExplorerListener
#endif
{
	const LbClusterConfig &config;
	FailureManager &failure_manager;
	BalancerMap &tcp_balancer;
	FilteredSocketStock &fs_stock;
	FilteredSocketBalancer &fs_balancer;
	LbMonitorStock *const monitors;

	const Logger logger;

	std::unique_ptr<SslSocketFilterParams> socket_filter_params;

	struct StaticMember {
		AllocatedSocketAddress address;

		FailurePtr failure;

		StaticMember(AllocatedSocketAddress &&_address,
			     ReferencedFailureInfo &_failure) noexcept
			:address(std::move(_address)),
			 failure(_failure) {}
	};

	std::vector<StaticMember> static_members;

#ifdef HAVE_AVAHI
	/**
	 * This #AvahiServiceExplorer locates Zeroconf nodes.
	 */
	std::unique_ptr<Avahi::ServiceExplorer> explorer;

	class StickyRing;

	/**
	 * For consistent hashing.  It is populated by FillActive().
	 */
	std::unique_ptr<StickyRing> sticky_ring;

	/**
	 * @see LbClusterConfig::sticky_cache
	 */
	std::unique_ptr<StickyCache> sticky_cache;
#endif

	/**
	 * A list of #LbMonitorRef instances, one for each static member
	 * (i.e. not Zeroconf).
	 */
	std::forward_list<LbMonitorRef> static_member_monitors;

#ifdef HAVE_AVAHI
	class ZeroconfMember;

	using ZeroconfMemberMap = std::map<std::string, ZeroconfMember, std::less<>>;

	using ZeroconfMemberList = std::vector<ZeroconfMemberMap::iterator>;

	struct ZeroconfListWrapper;

	/**
	 * All Zeroconf members.  Managed by our
	 * AvahiServiceExplorerListener virtual method overrides.
	 */
	ZeroconfMemberMap zeroconf_members;

	/**
	 * All #members pointers in a std::vector.  Populated by
	 * FillActive().
	 */
	ZeroconfMemberList active_zeroconf_members;

	/**
	 * This object selects the next Zeroconf member if
	 * StickyMode::NONE is configured.
	 */
	RoundRobinBalancer round_robin_balancer;

	bool dirty = false;

	class ZeroconfHttpConnect;
#endif

public:
	LbCluster(const LbClusterConfig &_config,
		  const LbContext &context,
		  LbMonitorStock *_monitors);
	~LbCluster() noexcept;

	const LbClusterConfig &GetConfig() const noexcept {
		return config;
	}

	/**
	 * Obtain a HTTP connection to a member (Zeroconf or static).
	 */
	void ConnectHttp(AllocatorPtr alloc,
			 const StopwatchPtr &parent_stopwatch,
			 uint_fast64_t fairness_hash,
			 SocketAddress bind_address,
			 Arch arch,
			 std::span<const std::byte> sticky_source,
			 sticky_hash_t sticky_hash,
			 Event::Duration timeout,
			 FilteredSocketBalancerHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Create a new TCP connection to a member (Zeroconf or
	 * static).
	 */
	void ConnectTcp(AllocatorPtr alloc,
			SocketAddress bind_address,
			std::span<const std::byte> sticky_source,
			Event::Duration timeout,
			ConnectSocketHandler &handler,
			CancellablePointer &cancel_ptr) noexcept;

private:
	/**
	 * Obtain a HTTP connection to a statically configured member
	 * (not Zeroconf).
	 */
	void ConnectStaticHttp(AllocatorPtr alloc,
			       const StopwatchPtr &parent_stopwatch,
			       uint_fast64_t fairness_hash,
			       SocketAddress bind_address,
			       sticky_hash_t sticky_hash,
			       Event::Duration timeout,
			       FilteredSocketBalancerHandler &handler,
			       CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Create a new TCP connection to a statically configured member
	 * (not Zeroconf).
	 */
	void ConnectStaticTcp(AllocatorPtr alloc,
			      SocketAddress bind_address,
			      sticky_hash_t sticky_hash,
			      Event::Duration timeout,
			      ConnectSocketHandler &handler,
			      CancellablePointer &cancel_ptr) noexcept;

#ifdef HAVE_AVAHI
	[[gnu::pure]]
	size_t GetZeroconfCount() noexcept {
		if (dirty) {
			dirty = false;
			FillActive();
		}

		return active_zeroconf_members.size();
	}

	/**
	 * Pick a member for the next request.
	 *
	 * Zeroconf only.
	 */
	ZeroconfMemberMap::const_pointer PickZeroconf(Expiry now, Arch arch,
						      std::span<const std::byte> sticky_source,
						      sticky_hash_t sticky_hash) noexcept;

	/**
	 * Like PickZeroconf(), but pick using Consistent Hashing (via
	 * #HashRing).
	 *
	 * To be called by PickZeroconf(), which has already
	 * lazy-initialized and verified everything.
	 */
	ZeroconfMemberMap::const_reference PickZeroconfHashRing(Expiry now,
								sticky_hash_t sticky_hash) noexcept;

	/**
	 * Like PickZeroconf(), but pick using Rendezvous Hashing.
	 *
	 * To be called by PickZeroconf(), which has already
	 * lazy-initialized and verified everything.
	 */
	ZeroconfMemberMap::const_reference PickZeroconfRendezvous(Expiry now, Arch arch,
								  std::span<const std::byte> sticky_source) noexcept;

	/**
	 * Like PickZeroconf(), but pick using #StickyCache.  Returns
	 * nullptr if the hash was not found in the cache.
	 *
	 * Zeroconf only.
	 */
	ZeroconfMemberMap::const_pointer PickZeroconfCache(Expiry now,
							   sticky_hash_t sticky_hash) noexcept;

	/**
	 * Obtain a HTTP connection to a Zeroconf member.
	 */
	void ConnectZeroconfHttp(AllocatorPtr alloc,
				 const StopwatchPtr &parent_stopwatch,
				 uint_fast64_t fairness_hash,
				 SocketAddress bind_address,
				 Arch arch,
				 std::span<const std::byte> sticky_source,
				 sticky_hash_t sticky_hash,
				 Event::Duration timeout,
				 FilteredSocketBalancerHandler &handler,
				 CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Create a new TCP connection to a Zeroconf member.
	 */
	void ConnectZeroconfTcp(AllocatorPtr alloc,
				SocketAddress bind_address,
				std::span<const std::byte> sticky_source,
				Event::Duration timeout,
				ConnectSocketHandler &handler,
				CancellablePointer &cancel_ptr) noexcept;

private:
	/**
	 * Fill #active_members and #sticky_ring.
	 *
	 * Zeroconf only.
	 */
	void FillActive() noexcept;

	/**
	 * Like PickNextZeroconf(), but skips members which are bad
	 * according to failure_get_status().  If all are bad, a random
	 * (bad) one is returned.
	 */
	ZeroconfMemberMap::const_reference PickNextGoodZeroconf(Expiry now) noexcept;

	/* virtual methods from class AvahiServiceExplorerListener */
	void OnAvahiNewObject(const std::string &key,
			      const InetAddress &address,
			      AvahiStringList *txt) noexcept override;
	void OnAvahiRemoveObject(const std::string &key) noexcept override;
#endif
};
