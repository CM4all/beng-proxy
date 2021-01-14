/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "Instance.hxx"
#include "tcp_stock.hxx"
#include "fs/Stock.hxx"
#include "stock/Stats.hxx"
#include "fb_pool.hxx"
#include "SlicePool.hxx"
#include "translation/Builder.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "nfs/Cache.hxx"
#include "session/Manager.hxx"
#include "session/Glue.hxx"
#include "AllocatorStats.hxx"
#include "beng-proxy/Control.hxx"
#include "util/ByteOrder.hxx"

BengProxy::ControlStats
BpInstance::GetStats() const noexcept
{
	BengProxy::ControlStats stats{};

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
	stats.children = ToBE32(child_process_registry.GetCount());
	stats.sessions = ToBE32(session_manager->Count());
	stats.http_requests = ToBE64(http_request_counter);
	stats.http_traffic_received = ToBE64(http_traffic_received_counter);
	stats.http_traffic_sent = ToBE64(http_traffic_sent_counter);
	stats.translation_cache_size = ToBE64(tcache_stats.netto_size);
	stats.http_cache_size = ToBE64(http_cache_stats.netto_size);
	stats.filter_cache_size = ToBE64(fcache_stats.netto_size);

	stats.translation_cache_brutto_size = ToBE64(tcache_stats.brutto_size);
	stats.http_cache_brutto_size = ToBE64(http_cache_stats.brutto_size);
	stats.filter_cache_brutto_size = ToBE64(fcache_stats.brutto_size);

#ifdef HAVE_LIBNFS
	const auto nfs_cache_stats = nfs_cache_get_stats(*nfs_cache);
	stats.nfs_cache_size = ToBE64(nfs_cache_stats.netto_size);
	stats.nfs_cache_brutto_size = ToBE64(nfs_cache_stats.brutto_size);
#endif

	const auto io_buffers_stats = fb_pool_get().GetStats();
	stats.io_buffers_size = ToBE64(io_buffers_stats.netto_size);
	stats.io_buffers_brutto_size = ToBE64(io_buffers_stats.brutto_size);

	/* TODO: add stats from all worker processes;  */

	return stats;
}
