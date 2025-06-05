// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Cluster.hxx"
#include "ClusterConfig.hxx"
#include "MemberHash.hxx"
#include "Context.hxx"
#include "MonitorStock.hxx"
#include "MonitorRef.hxx"
#include "fs/Stock.hxx"
#include "fs/Balancer.hxx"
#include "fs/Handler.hxx"
#include "ssl/SslSocketFilterFactory.hxx"
#include "cluster/StickyCache.hxx"
#include "cluster/ConnectBalancer.hxx"
#include "cluster/RoundRobinBalancer.cxx"
#include "stock/GetHandler.hxx"
#include "http/Status.hxx"
#include "system/Error.hxx"
#include "event/Loop.hxx"
#include "net/PConnectSocket.hxx"
#include "net/FailureManager.hxx"
#include "net/FormatAddress.hxx"
#include "util/DereferenceIterator.hxx"
#include "util/djb_hash.hxx"
#include "util/FNVHash.hxx"
#include "util/LeakDetector.hxx"
#include "AllocatorPtr.hxx"
#include "HttpMessageResponse.hxx"
#include "lease.hxx"
#include "stopwatch.hxx"

#ifdef HAVE_AVAHI
#include "system/Arch.hxx"
#include "lib/avahi/Explorer.hxx"
#include "lib/avahi/StringListCast.hxx"

#include <cmath> // for std::log()
#include <limits> // for std::numeric_limits

#include <stdlib.h> // for strtod()
#endif

using std::string_view_literals::operator""sv;

/**
 * The hash algorithm we use for Rendezvous Hashing.  FNV1a is fast
 * and has just the right properties for a good distribution among all
 * nodes.
 *
 * DJB is inferior when the node addresses are too similar (which is
 * often the case when all nodes are on the same local network) and
 * when the sticky_source is too short (e.g. when database serial
 * numbers are used) due to its small prime (33).
 */
using RendezvousHashAlgorithm = FNV1aAlgorithm<FNVTraits<uint32_t>>;

[[gnu::pure]]
static sticky_hash_t
CalculateStickyHash(std::span<const std::byte> source) noexcept
{
	return source.data() != nullptr
		? static_cast<sticky_hash_t>(djb_hash(source))
		: sticky_hash_t{};
}

#ifdef HAVE_AVAHI

class LbCluster::StickyRing final
	: public MemberHashRing<ZeroconfMemberMap::iterator> {};

class LbCluster::ZeroconfMember final : LeakDetector {
	AllocatedSocketAddress address;

	const FailureRef failure;

	const std::unique_ptr<LbMonitorRef> monitor;

	mutable std::string log_name;

	/**
	 * The precalculated hash of #address for Rendezvous
	 * Hashing.
	 */
	sticky_hash_t address_hash;

	/**
	 * The weight of this node (received in a Zeroconf TXT
	 * record).  We store the negative value because this
	 * eliminates one minus operator from the method
	 * CalculateRendezvousScore().
	 */
	double negative_weight;

	/**
	 * A score for rendezvous hasing calculated from the hash of
	 * the sticky attribute of the current request (e.g. the
	 * "Host" header) and this server address.
	 */
	double rendezvous_score;

	Arch arch;

public:
	ZeroconfMember(std::string_view key,
		       Arch _arch, double _weight,
		       SocketAddress _address,
		       ReferencedFailureInfo &_failure,
		       LbMonitorStock *monitors) noexcept;
	~ZeroconfMember() noexcept;

	ZeroconfMember(const ZeroconfMember &) = delete;
	ZeroconfMember &operator=(const ZeroconfMember &) = delete;

	SocketAddress GetAddress() const noexcept {
		return address;
	}

	void Update(SocketAddress _address, Arch _arch, double _weight) noexcept;

	void CalculateRendezvousScore(std::span<const std::byte> sticky_source) noexcept;

	Arch GetArch() const noexcept {
		return arch;
	}

	double GetRendezvousScore() const noexcept {
		return rendezvous_score;
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
	const char *GetLogName(const char *key) const noexcept;
};

LbCluster::ZeroconfMember::ZeroconfMember(std::string_view key,
					  Arch _arch, double _weight,
					  SocketAddress _address,
					  ReferencedFailureInfo &_failure,
					  LbMonitorStock *monitors) noexcept
	:address(_address), failure(_failure),
	 monitor(monitors != nullptr
		 ? std::make_unique<LbMonitorRef>(monitors->Add(key, _address))
		 : std::unique_ptr<LbMonitorRef>()),
	 address_hash(RendezvousHashAlgorithm::BinaryHash(address.GetSteadyPart())),
	 negative_weight(-_weight),
	 arch(_arch)
{
}

LbCluster::ZeroconfMember::~ZeroconfMember() noexcept = default;

inline void
LbCluster::ZeroconfMember::Update(SocketAddress _address, Arch _arch, double _weight) noexcept
{
	arch = _arch;
	negative_weight = -_weight;
	address = _address;
	address_hash = RendezvousHashAlgorithm::BinaryHash(address.GetSteadyPart());
}

const char *
LbCluster::ZeroconfMember::GetLogName(const char *key) const noexcept
{
	if (log_name.empty()) {
		if (address.IsNull())
			return key;

		log_name = key;

		char buffer[512];
		if (ToString(buffer, address)) {
			log_name += " (";
			log_name += buffer;
			log_name += ")";
		}
	}

	return log_name.c_str();
}

/**
 * Convert a quasi-random unsigned 64 bit integer to a
 * double-precision float in the range 0..1, preserving as many bits
 * as possible.  The returned value has no arithmetic meaning; the
 * goal of this function is only to convert a hash value to a floating
 * point value.
 */
template<std::unsigned_integral I>
static constexpr double
UintToDouble(const I i) noexcept
{
	constexpr unsigned SRC_BITS = std::numeric_limits<I>::digits;

	/* the mantissa has 53 bits, and this is how many bits we can
	   preserve in the conversion */
	constexpr unsigned DEST_BITS = std::numeric_limits<double>::digits;

	if constexpr (DEST_BITS < SRC_BITS) {
		/* discard upper bits that don't fit into the mantissa */
		constexpr uint_least64_t mask = (~I{}) >> (SRC_BITS - DEST_BITS);
		constexpr double max = I{1} << DEST_BITS;

		return (i & mask) / max;
	} else {
		/* don't discard anything */
		static_assert(std::numeric_limits<uintmax_t>::digits > std::numeric_limits<I>::digits);
		constexpr double max = std::uintmax_t{1} << SRC_BITS;

		return i / max;
	}
}

inline void
LbCluster::ZeroconfMember::CalculateRendezvousScore(std::span<const std::byte> sticky_source) noexcept
{
	const auto rendezvous_hash = RendezvousHashAlgorithm::BinaryHash(sticky_source, address_hash);
	rendezvous_score = negative_weight / std::log(UintToDouble(rendezvous_hash));
}

#endif // HAVE_AVAHI

LbCluster::LbCluster(const LbClusterConfig &_config,
		     const LbContext &context,
		     LbMonitorStock *_monitors)
	:config(_config), failure_manager(context.failure_manager),
	 tcp_balancer(context.tcp_balancer),
	 fs_stock(context.fs_stock),
	 fs_balancer(context.fs_balancer),
	 monitors(_monitors),
	 logger("cluster " + config.name)
{
	if (config.ssl)
		socket_filter_params = std::make_unique<SslSocketFilterParams>
			(context.fs_stock.GetEventLoop(),
			 context.ssl_client_factory,
			 config.http_host.empty() ? nullptr : config.http_host.c_str(),
			 nullptr);

#ifdef HAVE_AVAHI
	if (config.HasZeroConf())
		explorer = config.zeroconf.Create(context.GetAvahiClient(),
						  *this, context.avahi_error_handler);
#endif

	static_members.reserve(config.members.size());

	const unsigned default_port = config.GetDefaultPort();
	for (const auto &member : config.members) {
		AllocatedSocketAddress address(member.node->address);
		if (member.port > 0)
			address.SetPort(member.port);
		else if (default_port > 0 && address.GetPort() == 0)
			address.SetPort(default_port);

		auto &failure = failure_manager.Make(address);

		static_members.emplace_back(std::move(address), failure);
	}

	if (monitors != nullptr)
		/* create monitors for "static" members */
		for (const auto &member : config.members)
			static_member_monitors.emplace_front(monitors->Add(*member.node,
									   member.port));
}

LbCluster::~LbCluster() noexcept = default;

void
LbCluster::ConnectHttp(AllocatorPtr alloc,
		       const StopwatchPtr &parent_stopwatch,
		       uint_fast64_t fairness_hash,
		       SocketAddress bind_address,
		       Arch arch,
		       std::span<const std::byte> sticky_source,
		       sticky_hash_t sticky_hash,
		       Event::Duration timeout,
		       FilteredSocketBalancerHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept
{
#ifdef HAVE_AVAHI
	if (config.HasZeroConf()) {
		ConnectZeroconfHttp(alloc, parent_stopwatch,
				    fairness_hash,
				    bind_address, arch,
				    sticky_source, sticky_hash,
				    timeout,
				    handler, cancel_ptr);
		return;
	}
#else
	(void)arch;
	(void)sticky_source;
#endif

	ConnectStaticHttp(alloc, parent_stopwatch,
			  fairness_hash,
			  bind_address, sticky_hash,
			  timeout,
			  handler, cancel_ptr);
}

void
LbCluster::ConnectTcp(AllocatorPtr alloc,
		      SocketAddress bind_address,
		      std::span<const std::byte> sticky_source,
		      Event::Duration timeout,
		      ConnectSocketHandler &handler,
		      CancellablePointer &cancel_ptr) noexcept
{
#ifdef HAVE_AVAHI
	if (config.HasZeroConf()) {
		ConnectZeroconfTcp(alloc, bind_address, sticky_source,
				   timeout, handler, cancel_ptr);
		return;
	}
#else
	(void)sticky_source;
#endif

	ConnectStaticTcp(alloc, bind_address,
			 CalculateStickyHash(sticky_source),
			 timeout, handler, cancel_ptr);
}

inline void
LbCluster::ConnectStaticHttp(AllocatorPtr alloc,
			     const StopwatchPtr &parent_stopwatch,
			     uint_fast64_t fairness_hash,
			     SocketAddress bind_address,
			     sticky_hash_t sticky_hash,
			     Event::Duration timeout,
			     FilteredSocketBalancerHandler &handler,
			     CancellablePointer &cancel_ptr) noexcept
{
	assert(config.protocol == LbProtocol::HTTP);

	fs_balancer.Get(alloc, parent_stopwatch,
			fairness_hash,
			config.transparent_source,
			bind_address,
			sticky_hash,
			config.address_list,
			timeout,
			socket_filter_params.get(),
			handler, cancel_ptr);
}

inline void
LbCluster::ConnectStaticTcp(AllocatorPtr alloc,
			    SocketAddress bind_address,
			    sticky_hash_t sticky_hash,
			    Event::Duration timeout,
			    ConnectSocketHandler &handler,
			    CancellablePointer &cancel_ptr) noexcept
{
	assert(config.protocol == LbProtocol::TCP);

	client_balancer_connect(fs_balancer.GetEventLoop(), alloc,
				tcp_balancer,
				failure_manager,
				config.transparent_source,
				bind_address,
				sticky_hash,
				config.address_list,
				timeout,
				handler,
				cancel_ptr);
}

#ifdef HAVE_AVAHI

struct LbCluster::ZeroconfListWrapper {
	const ZeroconfMemberList &active_members;

	using const_reference = ZeroconfMemberMap::const_reference;
	using const_iterator = DereferenceIterator<ZeroconfMemberList::const_iterator>;

	auto size() const noexcept {
		return active_members.size();
	}

	const_iterator begin() const noexcept {
		return active_members.begin();
	}

	const_iterator end() const noexcept {
		return active_members.end();
	}

	[[gnu::pure]]
	bool Check(const Expiry now, const_reference member,
		   bool allow_fade) const noexcept {
		return member.second.GetFailureInfo().Check(now, allow_fade);
	}
};

LbCluster::ZeroconfMemberMap::const_reference
LbCluster::PickNextGoodZeroconf(const Expiry now) noexcept
{
	assert(!active_zeroconf_members.empty());

	if (active_zeroconf_members.size() < 2)
		/* since RoundRobinBalancer expects at least 2
		   members, this special case returns the one and only
		   member without consulting RoundRobinBalancer */
		return *active_zeroconf_members.front();

	return round_robin_balancer.Get(now,
					ZeroconfListWrapper{active_zeroconf_members},
					false);
}

inline LbCluster::ZeroconfMemberMap::const_reference
LbCluster::PickZeroconfHashRing(Expiry now,
				sticky_hash_t sticky_hash) noexcept
{
	assert(!active_zeroconf_members.empty());
	assert(sticky_ring != nullptr);

	auto i = sticky_ring->Pick(sticky_hash);
	assert(i != zeroconf_members.end());

	unsigned retries = active_zeroconf_members.size();
	while (true) {
		if (--retries == 0 ||
		    i->second.GetFailureInfo().Check(now))
			return *i;

		/* the node is known-bad; pick the next one in the ring */
		const auto next = sticky_ring->FindNext(sticky_hash);
		sticky_hash = next.first;
		i = next.second;
	}
}

inline LbCluster::ZeroconfMemberMap::const_reference
LbCluster::PickZeroconfRendezvous(Expiry now, const Arch arch,
				  std::span<const std::byte> sticky_source) noexcept
{
	assert(!active_zeroconf_members.empty());

	for (auto &i : active_zeroconf_members)
		i->second.CalculateRendezvousScore(sticky_source);

	/* sort the list of active Zeroconf members by a mix of its
	   address hash and the request's hash */
	std::sort(active_zeroconf_members.begin(),
		  active_zeroconf_members.end(),
		  [arch](ZeroconfMemberMap::const_iterator a,
			 ZeroconfMemberMap::const_iterator b) noexcept {
			  if (arch != Arch::NONE &&
			      a->second.GetArch() != b->second.GetArch()) {
				  [[unlikely]]

				  if (a->second.GetArch() == arch)
					  return true;
				  if (b->second.GetArch() == arch)
					  return false;
			  }

			  return a->second.GetRendezvousScore() > b->second.GetRendezvousScore();
		  });

	/* return the first "good" member */
	for (const auto &i : active_zeroconf_members) {
		if (i->second.GetFailureInfo().Check(now))
			return *i;
	}

	/* all are "bad" - return the "best" "bad" one */
	return *active_zeroconf_members.front();
}

inline LbCluster::ZeroconfMemberMap::const_pointer
LbCluster::PickZeroconfCache(Expiry now,
			     sticky_hash_t sticky_hash) noexcept
{
	/* look up the sticky_hash in the StickyCache */
	if (sticky_cache == nullptr)
		/* lazy cache allocation */
		sticky_cache = std::make_unique<StickyCache>();

	const auto *cached = sticky_cache->Get(sticky_hash);
	if (cached != nullptr) {
		/* cache hit */
		if (auto i = zeroconf_members.find(*cached);
		    i != zeroconf_members.end() &&
		    // TODO: allow FAILURE_FADE here?
		    i->second.GetFailureInfo().Check(now))
			/* the node is active, we can use it */
			return &*i;

		sticky_cache->Remove(sticky_hash);
	}

	return nullptr;
}

LbCluster::ZeroconfMemberMap::const_pointer
LbCluster::PickZeroconf(const Expiry now, Arch arch,
			std::span<const std::byte> sticky_source,
			sticky_hash_t sticky_hash) noexcept
{
	if (dirty) {
		dirty = false;
		FillActive();
	}

	if (active_zeroconf_members.empty())
		return nullptr;

	if (sticky_hash != 0) {
		assert(config.sticky_mode != StickyMode::NONE);

		switch (config.sticky_method) {
		case LbClusterConfig::StickyMethod::CONSISTENT_HASHING:
			return &PickZeroconfHashRing(now, sticky_hash);

		case LbClusterConfig::StickyMethod::RENDEZVOUS_HASHING:
			return &PickZeroconfRendezvous(now, arch, sticky_source);

		case LbClusterConfig::StickyMethod::CACHE:
			if (const auto *member = PickZeroconfCache(now,
								   sticky_hash))
				return member;

			/* cache miss or cached node not active: fall
			   back to round-robin and remember the new
			   pick in the cache */
			break;
		}
	}

	auto &i = PickNextGoodZeroconf(now);

	if (sticky_hash != 0)
		sticky_cache->Put(sticky_hash, i.first);

	return &i;
}

void
LbCluster::FillActive() noexcept
{
	round_robin_balancer.Reset();

	active_zeroconf_members.clear();
	active_zeroconf_members.reserve(zeroconf_members.size());

	for (auto i = zeroconf_members.begin(); i != zeroconf_members.end(); ++i)
		active_zeroconf_members.push_back(i);

	switch (config.sticky_method) {
	case LbClusterConfig::StickyMethod::CONSISTENT_HASHING:
		if (sticky_ring == nullptr)
			/* lazy allocation */
			sticky_ring = std::make_unique<StickyRing>();

		BuildMemberHashRing(*sticky_ring, active_zeroconf_members,
				    [](ZeroconfMemberMap::const_iterator member) noexcept {
					    return member->second.GetAddress();
				    });

		break;

	case LbClusterConfig::StickyMethod::RENDEZVOUS_HASHING:
	case LbClusterConfig::StickyMethod::CACHE:
		break;
	}
}

class LbCluster::ZeroconfHttpConnect final : StockGetHandler, Lease, Cancellable {
	LbCluster &cluster;

	AllocatorPtr alloc;

	const uint_least64_t fairness_hash;

	const SocketAddress bind_address;
	const std::span<const std::byte> sticky_source;
	const sticky_hash_t sticky_hash;
	const Event::Duration timeout;
	const SocketFilterParams *const filter_params;

	FilteredSocketBalancerHandler &handler;

	FailurePtr failure;

	CancellablePointer cancel_ptr;

	StockItem *stock_item;

	/**
	 * The number of remaining connection attempts.  We give up when
	 * we get an error and this attribute is already zero.
	 */
	unsigned retries;

	const Arch arch;

public:
	ZeroconfHttpConnect(LbCluster &_cluster, AllocatorPtr _alloc,
			    const uint_fast64_t _fairness_hash,
			    SocketAddress _bind_address,
			    Arch _arch,
			    std::span<const std::byte> _sticky_source,
			    sticky_hash_t _sticky_hash,
			    Event::Duration _timeout,
			    const SocketFilterParams *_filter_params,
			    FilteredSocketBalancerHandler &_handler,
			    CancellablePointer &caller_cancel_ptr) noexcept
		:cluster(_cluster), alloc(_alloc),
		 fairness_hash(_fairness_hash),
		 bind_address(_bind_address),
		 sticky_source(_sticky_source),
		 sticky_hash(_sticky_hash),
		 timeout(_timeout),
		 filter_params(_filter_params),
		 handler(_handler),
		 retries(CalculateRetries(cluster.GetZeroconfCount())),
		 arch(_arch)
	{
		caller_cancel_ptr = *this;
	}

	void Destroy() noexcept {
		this->~ZeroconfHttpConnect();
	}

	auto &GetEventLoop() const noexcept {
		return cluster.fs_balancer.GetEventLoop();
	}

	void Start() noexcept;

private:
	/* code copied from generic_balancer.hxx */
	static constexpr unsigned CalculateRetries(size_t size) noexcept {
		if (size <= 1)
			return 0;
		else if (size == 2)
			return 1;
		else if (size == 3)
			return 2;
		else
			return 3;
	}

	/* virtual methods from class StockGetHandler */
	void OnStockItemReady(StockItem &item) noexcept override;
	void OnStockItemError(std::exception_ptr ep) noexcept override;

	/* virtual methods from class Lease */
	PutAction ReleaseLease(PutAction action) noexcept final;

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}
};

void
LbCluster::ZeroconfHttpConnect::Start() noexcept
{
	auto *member = cluster.PickZeroconf(GetEventLoop().SteadyNow(),
					    arch,
					    sticky_source,
					    sticky_hash);
	if (member == nullptr) {
		auto &_handler = handler;
		Destroy();
		_handler.OnFilteredSocketError(std::make_exception_ptr(HttpMessageResponse(HttpStatus::SERVICE_UNAVAILABLE,
											   "Zeroconf cluster is empty")));
		return;
	}

	failure = member->second.GetFailureRef();

	cluster.fs_stock.Get(alloc,
			     nullptr,
			     member->second.GetLogName(member->first.c_str()),
			     fairness_hash,
			     cluster.config.transparent_source,
			     bind_address,
			     member->second.GetAddress(),
			     timeout, filter_params,
			     *this, cancel_ptr);
}

void
LbCluster::ZeroconfHttpConnect::OnStockItemReady(StockItem &item) noexcept
{
	failure->UnsetConnect();

	stock_item = &item;

	handler.OnFilteredSocketReady(*this, fs_stock_item_get(item),
				      fs_stock_item_get_address(item),
				      item.GetStockNameC(),
				      *failure);
}

void
LbCluster::ZeroconfHttpConnect::OnStockItemError(std::exception_ptr ep) noexcept
{
	failure->SetConnect(GetEventLoop().SteadyNow(),
			    std::chrono::seconds(20));

	if (retries-- > 0) {
		/* try the next Zeroconf member */
		Start();
		return;
	}

	auto &_handler = handler;
	Destroy();
	_handler.OnFilteredSocketError(std::move(ep));
}

PutAction
LbCluster::ZeroconfHttpConnect::ReleaseLease(PutAction action) noexcept
{
	auto &_item = *stock_item;
	Destroy();
	return _item.Put(action);
}

inline void
LbCluster::ConnectZeroconfHttp(AllocatorPtr alloc,
			       const StopwatchPtr &,
			       uint_fast64_t fairness_hash,
			       SocketAddress bind_address,
			       Arch arch,
			       std::span<const std::byte> sticky_source,
			       sticky_hash_t sticky_hash,
			       Event::Duration timeout,
			       FilteredSocketBalancerHandler &handler,
			       CancellablePointer &cancel_ptr) noexcept
{
	assert(config.HasZeroConf());

	auto *c = alloc.New<ZeroconfHttpConnect>(*this, alloc,
						 fairness_hash,
						 bind_address,
						 arch,
						 sticky_source, sticky_hash,
						 timeout,
						 socket_filter_params.get(),
						 handler, cancel_ptr);
	c->Start();
}

inline void
LbCluster::ConnectZeroconfTcp(AllocatorPtr alloc,
			      SocketAddress bind_address,
			      std::span<const std::byte> sticky_source,
			      Event::Duration timeout,
			      ConnectSocketHandler &handler,
			      CancellablePointer &cancel_ptr) noexcept
{
	assert(config.HasZeroConf());
	assert(config.protocol == LbProtocol::TCP);

	auto &event_loop = fs_balancer.GetEventLoop();

	const auto *member = PickZeroconf(event_loop.SteadyNow(),
					  Arch::NONE,
					  sticky_source,
					  CalculateStickyHash(sticky_source));
	if (member == nullptr) {
		handler.OnSocketConnectError(std::make_exception_ptr(std::runtime_error("Zeroconf cluster is empty")));
		return;
	}

	const auto address = member->second.GetAddress();
	assert(address.IsDefined());

	client_socket_new(event_loop, alloc, nullptr,
			  address.GetFamily(), SOCK_STREAM, 0,
			  config.transparent_source, bind_address,
			  address,
			  timeout,
			  handler, cancel_ptr);
}

[[gnu::pure]]
static Arch
GetArchFromTxt(AvahiStringList *txt) noexcept
{
	constexpr std::string_view prefix = "arch="sv;
	txt = avahi_string_list_find(txt, "arch");
	return txt != nullptr
		? ParseArch(Avahi::ToStringView(*txt).substr(prefix.size()))
		: Arch::NONE;
}

[[gnu::pure]]
static double
GetWeightFromTxt(AvahiStringList *txt) noexcept
{
	constexpr std::string_view prefix = "weight="sv;
	txt = avahi_string_list_find(txt, "weight");
	if (txt == nullptr)
		/* there's no "weight" record */
		return 1.0;

	const char *s = reinterpret_cast<const char *>(txt->text) + prefix.size();
	char *endptr;
	double value = strtod(s, &endptr);
	if (endptr == s || *endptr != '\0' || value <= 0 || value > 1e6)
		/* parser failed: fall back to default value */
		return 1.0;

	return value;
}

void
LbCluster::OnAvahiNewObject(const std::string &key,
			    SocketAddress address,
			    AvahiStringList *txt) noexcept
{
	const auto arch = GetArchFromTxt(txt);
	const auto weight = GetWeightFromTxt(txt);

	auto [it, inserted] = zeroconf_members.try_emplace(key, key, arch, weight, address,
							   failure_manager.Make(address),
							   monitors);
	if (!inserted) {
		/* update existing member */
		it->second.Update(address, arch, weight);
	}

	dirty = true;
}

void
LbCluster::OnAvahiRemoveObject(const std::string &key) noexcept
{
	auto i = zeroconf_members.find(key);
	if (i == zeroconf_members.end())
		return;

	/* TODO: purge entry from the "failure" map, because it
	   will never be used again anyway */

	zeroconf_members.erase(i);
	dirty = true;
}

#endif // HAVE_AVAHI
