/*
 * Collect statistics of a beng-lb process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "bp_stats.h"
#include "instance.h"
#include "stock.h"
#include "cache.h"
#include "tcache.h"
#include "http-cache.h"
#include "fcache.h"
#include "child.h"
#include "session_manager.h"
#include "beng-proxy/control.h"

uint64_t http_request_counter;

void
bp_get_stats(const struct instance *instance,
             struct beng_control_stats *data)
{
    struct stock_stats tcp_stock_stats = {
        .busy = 0,
        .idle = 0,
    };

    hstock_add_stats(instance->tcp_stock, &tcp_stock_stats);

    struct cache_stats tcache_stats, http_cache_stats, fcache_stats;
    translate_cache_get_stats(instance->translate_cache, &tcache_stats);
    http_cache_get_stats(instance->http_cache, &http_cache_stats);
    filter_cache_get_stats(instance->filter_cache, &fcache_stats);

    data->incoming_connections = GUINT32_TO_BE(instance->num_connections);
    data->outgoing_connections = GUINT32_TO_BE(tcp_stock_stats.busy
                                               + tcp_stock_stats.idle);
    data->children = GUINT32_TO_BE(child_get_count());
    data->sessions = GUINT32_TO_BE(session_manager_get_count());
    data->http_requests = GUINT64_TO_BE(http_request_counter);
    data->translation_cache_size = GUINT64_TO_BE(tcache_stats.size);
    data->http_cache_size = GUINT64_TO_BE(http_cache_stats.size);
    data->filter_cache_size = GUINT64_TO_BE(fcache_stats.size);

    /* TODO: add stats from all worker processes;  */
}
