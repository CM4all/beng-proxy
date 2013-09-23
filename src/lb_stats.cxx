/*
 * Collect statistics of a beng-lb process.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lb_stats.hxx"
#include "lb_instance.hxx"
#include "hstock.h"
#include "stock.h"
#include "beng-proxy/control.h"

void
lb_get_stats(const struct lb_instance *instance,
             struct beng_control_stats *data)
{
    struct stock_stats tcp_stock_stats = {
        .busy = 0,
        .idle = 0,
    };

    hstock_add_stats(instance->tcp_stock, &tcp_stock_stats);

    data->incoming_connections = GUINT32_TO_BE(instance->num_connections);
    data->outgoing_connections = GUINT32_TO_BE(tcp_stock_stats.busy
                                               + tcp_stock_stats.idle);
    data->children = 0;
    data->sessions = 0;
    data->http_requests = GUINT64_TO_BE(instance->http_request_counter);
    data->translation_cache_size = 0;
    data->http_cache_size = 0;
    data->filter_cache_size = 0;
}
