// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Instance.hxx"
#include "prometheus/Stats.hxx"
#include "fs/Stock.hxx"
#include "stock/Stats.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SlicePool.hxx"
#include "memory/AllocatorStats.hxx"
#include "translation/Builder.hxx"
#include "http/cache/FilterCache.hxx"
#include "http/cache/Public.hxx"
#include "session/Manager.hxx"
#include "net/control/Protocol.hxx"
#include "util/ByteOrder.hxx"
#include "tcp_stock.hxx"

Prometheus::Stats
BpInstance::GetStats() const noexcept
{
	Prometheus::Stats stats{};

	StockStats tcp_stock_stats{};

	tcp_stock->AddStats(tcp_stock_stats);
	fs_stock->AddStats(tcp_stock_stats);

	AllocatorStats tcache_stats = AllocatorStats::Zero();
	if (translation_caches)
		tcache_stats += translation_caches->GetStats();
	const auto http_cache_stats = http_cache != nullptr
		? http_cache_get_stats(*http_cache)
		: AllocatorStats::Zero();
	const auto fcache_stats = filter_cache != nullptr
		? filter_cache_get_stats(*filter_cache)
		: AllocatorStats::Zero();

	stats.incoming_connections = ToBE32(connections.size());
	stats.outgoing_connections = ToBE32(tcp_stock_stats.busy
					    + tcp_stock_stats.idle);
	stats.children = 0; // TODO
	stats.sessions = ToBE32(session_manager->Count());
	stats.http_requests = ToBE64(http_stats.n_requests);
	stats.http_traffic_received = ToBE64(http_stats.traffic_received);
	stats.http_traffic_sent = ToBE64(http_stats.traffic_sent);
	stats.translation_cache_size = ToBE64(tcache_stats.netto_size);
	stats.http_cache_size = ToBE64(http_cache_stats.netto_size);
	stats.filter_cache_size = ToBE64(fcache_stats.netto_size);

	stats.translation_cache_brutto_size = ToBE64(tcache_stats.brutto_size);
	stats.http_cache_brutto_size = ToBE64(http_cache_stats.brutto_size);
	stats.filter_cache_brutto_size = ToBE64(fcache_stats.brutto_size);

	const auto io_buffers_stats = fb_pool_get().GetStats();
	stats.io_buffers_size = ToBE64(io_buffers_stats.netto_size);
	stats.io_buffers_brutto_size = ToBE64(io_buffers_stats.brutto_size);

	return stats;
}
