// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Instance.hxx"
#include "fs/Stock.hxx"
#include "stock/Stats.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SlicePool.hxx"
#include "stats/AllocatorStats.hxx"
#include "net/control/Protocol.hxx"
#include "util/ByteOrder.hxx"

BengProxy::ControlStats
LbInstance::GetStats() const noexcept
{
	BengProxy::ControlStats stats;

	StockStats tcp_stock_stats{};

	fs_stock->AddStats(tcp_stock_stats);

	stats.incoming_connections = ToBE32(http_connections.size()
					    + tcp_connections.size());
	stats.outgoing_connections = ToBE32(tcp_stock_stats.busy
					    + tcp_stock_stats.idle
					    + tcp_connections.size());
	stats.children = 0;
	stats.sessions = 0;
	stats.http_requests = ToBE64(http_stats.n_requests);
	stats.http_traffic_received = ToBE64(http_stats.traffic_received);
	stats.http_traffic_sent = ToBE64(http_stats.traffic_sent);
	stats.translation_cache_size = ToBE64(goto_map.GetAllocatedTranslationCacheMemory());
	stats.http_cache_size = 0;
	stats.filter_cache_size = 0;
	stats.translation_cache_brutto_size = stats.translation_cache_size;
	stats.http_cache_brutto_size = 0;
	stats.filter_cache_brutto_size = 0;
	stats.nfs_cache_size = stats.nfs_cache_brutto_size = 0;

	const auto io_buffers_stats = fb_pool_get().GetStats();
	stats.io_buffers_size = ToBE64(io_buffers_stats.netto_size);
	stats.io_buffers_brutto_size = ToBE64(io_buffers_stats.brutto_size);

	return stats;
}
