/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ACCESS_LOG_DATAGRAM_HXX
#define ACCESS_LOG_DATAGRAM_HXX

#include <http/method.h>
#include <http/status.h>

#include <stdint.h>

struct log_datagram {
    uint64_t timestamp;

    const char *remote_host, *site;

    http_method_t http_method;

    const char *http_uri, *http_referer, *user_agent;

    http_status_t http_status;

    uint64_t length;

    uint64_t traffic_received, traffic_sent;

    uint64_t duration;

    bool valid_timestamp, valid_http_method, valid_http_status;
    bool valid_length, valid_traffic;
    bool valid_duration;
};

#endif
