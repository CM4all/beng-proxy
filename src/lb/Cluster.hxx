// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "cluster/StickyHash.hxx"
#include "cluster/RoundRobinBalancer.hxx"
#include "event/Chrono.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/FailureRef.hxx"
#include "io/Logger.hxx"
#include "util/LeakDetector.hxx"

#ifdef HAVE_AVAHI
#include "lib/avahi/ExplorerListener.hxx"
#endif

#include <boost/intrusive/set.hpp>

#include <forward_list>
#include <vector>
#include <string>
#include <memory>

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
	class ZeroconfMember final
		: LeakDetector,
		  public boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

		const std::string key;

		AllocatedSocketAddress address;

		const FailureRef failure;

		const std::unique_ptr<LbMonitorRef> monitor;

		mutable std::string log_name;

	public:
		ZeroconfMember(const std::string &_key, SocketAddress _address,
			       ReferencedFailureInfo &_failure,
			       LbMonitorStock *monitors) noexcept;
		~ZeroconfMember() noexcept;

		ZeroconfMember(const ZeroconfMember &) = delete;
		ZeroconfMember &operator=(const ZeroconfMember &) = delete;

		std::string_view GetKey() const noexcept {
			return key;
		}

		SocketAddress GetAddress() const noexcept {
			return address;
		}

		void SetAddress(SocketAddress _address) noexcept {
			address = _address;
		}

		auto &GetFailureRef() const noexcept {
			return failure;
		}

		FailureInfo &GetFailureInfo() const noexcept {
			return *failure;
		}

		/**
		 * Obtain a name identifying this object for logging.
		 */
		[[gnu::pure]]
		const char *GetLogName() const noexcept;

		struct Compare {
			bool operator()(const ZeroconfMember &a,
					const ZeroconfMember &b) const noexcept {
				return a.key < b.key;
			}

			bool operator()(const ZeroconfMember &a,
					const std::string &b) const noexcept {
				return a.key < b;
			}

			bool operator()(const std::string &a,
					const ZeroconfMember &b) const noexcept {
				return a < b.key;
			}
		};
	};

	using ZeroconfMemberMap =
		boost::intrusive::set<ZeroconfMember,
				      boost::intrusive::compare<ZeroconfMember::Compare>,
				      boost::intrusive::constant_time_size<false>>;

	using ZeroconfMemberList = std::vector<ZeroconfMemberMap::pointer>;

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
			sticky_hash_t sticky_hash,
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
	const ZeroconfMember *PickZeroconf(Expiry now, sticky_hash_t sticky_hash) noexcept;

	/**
	 * Like PickZeroconf(), but pick using Consistent Hashing (via
	 * #HashRing).
	 *
	 * To be called by PickZeroconf(), which has already
	 * lazy-initialized and verified everything.
	 */
	const ZeroconfMember &PickZeroconfHashRing(Expiry now,
						   sticky_hash_t sticky_hash) noexcept;

	/**
	 * Obtain a HTTP connection to a Zeroconf member.
	 */
	void ConnectZeroconfHttp(AllocatorPtr alloc,
				 const StopwatchPtr &parent_stopwatch,
				 uint_fast64_t fairness_hash,
				 SocketAddress bind_address,
				 sticky_hash_t sticky_hash,
				 Event::Duration timeout,
				 FilteredSocketBalancerHandler &handler,
				 CancellablePointer &cancel_ptr) noexcept;

	/**
	 * Create a new TCP connection to a Zeroconf member.
	 */
	void ConnectZeroconfTcp(AllocatorPtr alloc,
				SocketAddress bind_address,
				sticky_hash_t sticky_hash,
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
			      SocketAddress address) noexcept override;
	void OnAvahiRemoveObject(const std::string &key) noexcept override;
#endif
};
