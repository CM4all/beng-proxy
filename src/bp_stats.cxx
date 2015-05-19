/*
 * Collect statistics of a beng-lb process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_stats.hxx"
#include "bp_instance.hxx"
#include "hstock.hxx"
#include "stock.hxx"
#include "cache.hxx"
#include "tcache.hxx"
#include "http_cache.hxx"
#include "fcache.hxx"
#include "child_manager.hxx"
#include "session_manager.hxx"
#include "beng-proxy/control.h"
#include "util/ByteOrder.hxx"

void
bp_get_stats(const struct instance *instance,
             struct beng_control_stats *data)
{
    StockStats tcp_stock_stats = {
        .busy = 0,
        .idle = 0,
    };

    hstock_add_stats(*instance->tcp_stock, tcp_stock_stats);

    struct cache_stats tcache_stats, http_cache_stats, fcache_stats;
    translate_cache_get_stats(*instance->translate_cache, tcache_stats);
    http_cache_get_stats(*instance->http_cache, http_cache_stats);
    filter_cache_get_stats(instance->filter_cache, &fcache_stats);

    data->incoming_connections = ToBE32(instance->num_connections);
    data->outgoing_connections = ToBE32(tcp_stock_stats.busy
                                               + tcp_stock_stats.idle);
    data->children = ToBE32(child_get_count());
    data->sessions = ToBE32(session_manager_get_count());
    data->http_requests = ToBE64(instance->http_request_counter);
    data->translation_cache_size = ToBE64(tcache_stats.netto_size);
    data->http_cache_size = ToBE64(http_cache_stats.netto_size);
    data->filter_cache_size = ToBE64(fcache_stats.netto_size);

    /* TODO: add stats from all worker processes;  */
}
