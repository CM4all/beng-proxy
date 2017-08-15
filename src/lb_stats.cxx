/*
 * Collect statistics of a beng-lb process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_stats.hxx"
#include "lb/Instance.hxx"
#include "stock/MapStock.hxx"
#include "stock/Stats.hxx"
#include "fb_pool.hxx"
#include "SlicePool.hxx"
#include "AllocatorStats.hxx"
#include "beng-proxy/control.h"
#include "util/ByteOrder.hxx"

void
lb_get_stats(const LbInstance *instance,
             struct beng_control_stats *data)
{
    StockStats tcp_stock_stats = {
        .busy = 0,
        .idle = 0,
    };

    instance->tcp_stock->AddStats(tcp_stock_stats);

    data->incoming_connections = ToBE32(instance->http_connections.size()
                                        + instance->tcp_connections.size());
    data->outgoing_connections = ToBE32(tcp_stock_stats.busy
                                        + tcp_stock_stats.idle
                                        + instance->tcp_connections.size());
    data->children = 0;
    data->sessions = 0;
    data->http_requests = ToBE64(instance->http_request_counter);
    data->translation_cache_size = 0;
    data->http_cache_size = 0;
    data->filter_cache_size = 0;
    data->translation_cache_brutto_size = 0;
    data->http_cache_brutto_size = 0;
    data->filter_cache_brutto_size = 0;
    data->nfs_cache_size = data->nfs_cache_brutto_size = 0;

    const auto io_buffers_stats = slice_pool_get_stats(fb_pool_get());
    data->io_buffers_size = ToBE64(io_buffers_stats.netto_size);
    data->io_buffers_brutto_size = ToBE64(io_buffers_stats.brutto_size);
}
