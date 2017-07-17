/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef ACCESS_LOG_DATAGRAM_HXX
#define ACCESS_LOG_DATAGRAM_HXX

#include <http/method.h>
#include <http/status.h>

#include <stdint.h>

struct AccessLogDatagram {
    uint64_t timestamp;

    const char *remote_host, *host, *site;

    http_method_t http_method;

    const char *http_uri, *http_referer, *user_agent;

    http_status_t http_status;

    uint64_t length;

    uint64_t traffic_received, traffic_sent;

    uint64_t duration;

    bool valid_timestamp, valid_http_method, valid_http_status;
    bool valid_length, valid_traffic;
    bool valid_duration;

    AccessLogDatagram() = default;

    AccessLogDatagram(uint64_t _timestamp,
                      http_method_t _method, const char *_uri,
                      const char *_remote_host,
                      const char *_host, const char *_site,
                      const char *_referer, const char *_user_agent,
                      http_status_t _status, int64_t _length,
                      uint64_t _traffic_received, uint64_t _traffic_sent,
                      uint64_t _duration)
        :timestamp(_timestamp),
         remote_host(_remote_host), host(_host), site(_site),
         http_method(_method),
         http_uri(_uri), http_referer(_referer), user_agent(_user_agent),
         http_status(_status),
         length(_length),
         traffic_received(_traffic_received), traffic_sent(_traffic_sent),
         duration(_duration),
         valid_timestamp(true),
         valid_http_method(true), valid_http_status(true),
         valid_length(_length >= 0), valid_traffic(true),
         valid_duration(true) {}
};

#endif
