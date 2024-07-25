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
#include "http/cache/EncodingCache.hxx"
#include "http/cache/FilterCache.hxx"
#include "http/cache/Public.hxx"
#include "session/Manager.hxx"
#include "net/control/Protocol.hxx"
#include "tcp_stock.hxx"

Prometheus::Stats
BpInstance::GetStats() const noexcept
{
	Prometheus::Stats stats{};

	StockStats tcp_stock_stats{};

	tcp_stock->AddStats(tcp_stock_stats);
	fs_stock->AddStats(tcp_stock_stats);

	stats.incoming_connections = connections.size();
	stats.outgoing_connections = tcp_stock_stats.busy + tcp_stock_stats.idle;
	stats.sessions = session_manager->Count();
	stats.http_requests = http_stats.n_requests;
	stats.http_traffic_received = http_stats.traffic_received;
	stats.http_traffic_sent = http_stats.traffic_sent;

	if (translation_caches)
		stats.translation_cache = translation_caches->GetStats();

	if (http_cache != nullptr)
		stats.http_cache = http_cache_get_stats(*http_cache);

	if (filter_cache != nullptr)
		stats.filter_cache = filter_cache_get_stats(*filter_cache);

	if (encoding_cache)
		stats.encoding_cache = encoding_cache->GetStats();

	stats.io_buffers = fb_pool_get().GetStats();

	return stats;
}
