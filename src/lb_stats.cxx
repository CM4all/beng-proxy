/*
 * Collect statistics of a beng-lb process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_stats.hxx"
#include "lb_instance.hxx"
#include "hstock.hxx"
#include "stock.hxx"
#include "beng-proxy/control.h"
#include "util/ByteOrder.hxx"

void
lb_get_stats(const struct lb_instance *instance,
             struct beng_control_stats *data)
{
    StockStats tcp_stock_stats = {
        .busy = 0,
        .idle = 0,
    };

    hstock_add_stats(*instance->tcp_stock, tcp_stock_stats);

    data->incoming_connections = ToBE32(instance->connections.size());
    data->outgoing_connections = ToBE32(tcp_stock_stats.busy
                                        + tcp_stock_stats.idle);
    data->children = 0;
    data->sessions = 0;
    data->http_requests = ToBE64(instance->http_request_counter);
    data->translation_cache_size = 0;
    data->http_cache_size = 0;
    data->filter_cache_size = 0;
}
