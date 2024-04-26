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
#include "net/control/Protocol.hxx"

Prometheus::Stats
LbInstance::GetStats() const noexcept
{
	Prometheus::Stats stats{};

	StockStats tcp_stock_stats{};

	fs_stock->AddStats(tcp_stock_stats);

	stats.incoming_connections = http_connections.size() +
		tcp_connections.size();
	stats.outgoing_connections = tcp_stock_stats.busy +
		tcp_stock_stats.idle +
		tcp_connections.size();
	stats.http_requests = http_stats.n_requests;
	stats.http_traffic_received = http_stats.traffic_received;
	stats.http_traffic_sent = http_stats.traffic_sent;
	stats.translation_cache.allocator.netto_size =
		stats.translation_cache.allocator.brutto_size =
		goto_map.GetAllocatedTranslationCacheMemory();

	const auto io_buffers_stats = fb_pool_get().GetStats();
	stats.io_buffers_size = io_buffers_stats.netto_size;
	stats.io_buffers_brutto_size = io_buffers_stats.brutto_size;

	return stats;
}
