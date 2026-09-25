// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Instance.hxx"
#include "Listener.hxx"
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

	for (const auto &i : listeners)
		stats.incoming_handshaking_connections += i.GetPendingCount();

	stats.incoming_connections = http_connections.size() +
		tcp_connections.size();
	stats.outgoing_connections = tcp_stock_stats.busy +
		tcp_stock_stats.idle +
		tcp_connections.size();
	stats.http_requests = http_stats.n_requests;
	stats.http_traffic_received = http_stats.traffic_received;
	stats.http_traffic_sent = http_stats.traffic_sent;
	stats.translation_cache = goto_map.GetTranslationCacheStats();

	stats.io_buffers = fb_pool_get().GetStats();

	return stats;
}
