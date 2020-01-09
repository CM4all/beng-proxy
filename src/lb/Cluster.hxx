/*
 * Copyright 2007-2019 CM4all GmbH
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

#pragma once

#include "cluster/StickyHash.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "net/FailureRef.hxx"
#include "io/Logger.hxx"
#include "util/LeakDetector.hxx"

#ifdef HAVE_AVAHI
#include "avahi/ExplorerListener.hxx"
#endif

#include <boost/intrusive/set.hpp>

#include <forward_list>
#include <vector>
#include <string>
#include <memory>

struct LbConfig;
struct LbGotoIfConfig;
struct LbListenerConfig;
struct LbClusterConfig;
class LbMonitorStock;
class LbMonitorRef;
class FailureManager;
class MyAvahiClient;
class StickyCache;
class AvahiServiceExplorer;

class LbCluster final
#ifdef HAVE_AVAHI
	: AvahiServiceExplorerListener
#endif
{
	const LbClusterConfig &config;
	FailureManager &failure_manager;
	LbMonitorStock *const monitors;

	const Logger logger;

#ifdef HAVE_AVAHI
	/**
	 * This #AvahiServiceExplorer locates Zeroconf nodes.
	 */
	std::unique_ptr<AvahiServiceExplorer> explorer;

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
	class Member
		: LeakDetector,
		  public boost::intrusive::set_base_hook<boost::intrusive::link_mode<boost::intrusive::normal_link>> {

		const std::string key;

		AllocatedSocketAddress address;

		FailureRef failure;

		std::unique_ptr<LbMonitorRef> monitor;

		mutable std::string log_name;

		unsigned refs = 1;

	public:
		Member(const std::string &_key, SocketAddress _address,
		       ReferencedFailureInfo &_failure,
		       LbMonitorStock *monitors);
		~Member() noexcept;

		Member(const Member &) = delete;
		Member &operator=(const Member &) = delete;

		void Ref() noexcept {
			++refs;
		}

		void Unref() noexcept {
			if (--refs == 0)
				delete this;
		}

		const std::string &GetKey() const noexcept {
			return key;
		}

		SocketAddress GetAddress() const noexcept {
			return address;
		}

		void SetAddress(SocketAddress _address) noexcept {
			address = _address;
		}

		auto &GetFailureRef() noexcept {
			return failure;
		}

		FailureInfo &GetFailureInfo() noexcept {
			return *failure;
		}

		/**
		 * Obtain a name identifying this object for logging.
		 */
		gcc_pure
		const char *GetLogName() const noexcept;

		struct Compare {
			bool operator()(const Member &a, const Member &b) const noexcept {
				return a.key < b.key;
			}

			bool operator()(const Member &a, const std::string &b) const noexcept {
				return a.key < b;
			}

			bool operator()(const std::string &a, const Member &b) const noexcept {
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

	/**
	 * All Zeroconf members.  Managed by our
	 * AvahiServiceExplorerListener virtual method overrides.
	 */
	MemberMap members;

	/**
	 * All #members pointers in a std::vector.  Populated by
	 * FillActive().
	 */
	std::vector<MemberMap::pointer> active_members;

	bool dirty = false;

	unsigned last_pick = 0;
#endif

public:
	LbCluster(const LbClusterConfig &_config, FailureManager &_failure_manager,
		  LbMonitorStock *_monitors
#ifdef HAVE_AVAHI
		  , MyAvahiClient &avahi_client
#endif
		  );
	~LbCluster() noexcept;

	const LbClusterConfig &GetConfig() const noexcept {
		return config;
	}

#ifdef HAVE_AVAHI
	gcc_pure
	size_t GetZeroconfCount() noexcept {
		if (dirty) {
			dirty = false;
			FillActive();
		}

		return active_members.size();
	}

	/**
	 * Pick a member for the next request.
	 *
	 * Zeroconf only.
	 */
	Member *Pick(Expiry now, sticky_hash_t sticky_hash) noexcept;

private:
	/**
	 * Fill #active_members and #sticky_ring.
	 *
	 * Zeroconf only.
	 */
	void FillActive() noexcept;

	/**
	 * Pick the next active Zeroconf member in a round-robin way.
	 * Does not update the #StickyCache.
	 */
	MemberMap::reference PickNextZeroconf() noexcept;

	/**
	 * Like PickNextZeroconf(), but skips members which are bad
	 * according to failure_get_status().  If all are bad, a random
	 * (bad) one is returned.
	 */
	MemberMap::reference PickNextGoodZeroconf(Expiry now) noexcept;

	/* virtual methods from class AvahiServiceExplorerListener */
	void OnAvahiNewObject(const std::string &key,
			      SocketAddress address) noexcept override;
	void OnAvahiRemoveObject(const std::string &key) noexcept override;
#endif
};
