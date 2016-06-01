/*
 * Collect statistics of a beng-lb process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_stats.hxx"
#include "bp_instance.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stats.hxx"
#include "fb_pool.hxx"
#include "SlicePool.hxx"
#include "tcache.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "nfs_cache.hxx"
#include "session_manager.hxx"
#include "AllocatorStats.hxx"
#include "beng-proxy/control.h"
#include "util/ByteOrder.hxx"

void
bp_get_stats(const BpInstance *instance,
             struct beng_control_stats *data)
{
    StockStats tcp_stock_stats = {
        .busy = 0,
        .idle = 0,
    };

    instance->tcp_stock->AddStats(tcp_stock_stats);

    const auto tcache_stats = instance->translate_cache != nullptr
        ? translate_cache_get_stats(*instance->translate_cache)
        : AllocatorStats::Zero();
    const auto http_cache_stats = http_cache_get_stats(*instance->http_cache);
    const auto fcache_stats = instance->filter_cache != nullptr
        ? filter_cache_get_stats(*instance->filter_cache)
        : AllocatorStats::Zero();

    data->incoming_connections = ToBE32(instance->connections.size());
    data->outgoing_connections = ToBE32(tcp_stock_stats.busy
                                               + tcp_stock_stats.idle);
    data->children = ToBE32(instance->child_process_registry.GetCount());
    data->sessions = ToBE32(session_manager_get_count());
    data->http_requests = ToBE64(instance->http_request_counter);
    data->translation_cache_size = ToBE64(tcache_stats.netto_size);
    data->http_cache_size = ToBE64(http_cache_stats.netto_size);
    data->filter_cache_size = ToBE64(fcache_stats.netto_size);

    data->translation_cache_brutto_size = ToBE64(tcache_stats.brutto_size);
    data->http_cache_brutto_size = ToBE64(http_cache_stats.brutto_size);
    data->filter_cache_brutto_size = ToBE64(fcache_stats.brutto_size);

#ifdef HAVE_LIBNFS
    const auto nfs_cache_stats = nfs_cache_get_stats(*instance->nfs_cache);
#else
    const auto nfs_cache_stats = AllocatorStats::Zero();
#endif
    data->nfs_cache_size = ToBE64(nfs_cache_stats.netto_size);
    data->nfs_cache_brutto_size = ToBE64(nfs_cache_stats.brutto_size);

    const auto io_buffers_stats = slice_pool_get_stats(fb_pool_get());
    data->io_buffers_size = ToBE64(io_buffers_stats.netto_size);
    data->io_buffers_brutto_size = ToBE64(io_buffers_stats.brutto_size);

    /* TODO: add stats from all worker processes;  */
}
