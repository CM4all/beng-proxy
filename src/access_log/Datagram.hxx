/*
 * Copyright 2007-2017 Content Management AG
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

#ifndef ACCESS_LOG_DATAGRAM_HXX
#define ACCESS_LOG_DATAGRAM_HXX

#include "http/Method.h"
#include "http/Status.h"
#include "util/StringView.hxx"

#include <chrono>

#include <stdint.h>

struct AccessLogDatagram {
    uint64_t timestamp;

    const char *remote_host, *host, *site;

    const char *forwarded_to;

    http_method_t http_method;

    const char *http_uri, *http_referer, *user_agent;

    StringView message;

    http_status_t http_status;

    uint64_t length;

    uint64_t traffic_received, traffic_sent;

    uint64_t duration;

    bool valid_timestamp, valid_http_method, valid_http_status;
    bool valid_length, valid_traffic;
    bool valid_duration;

    AccessLogDatagram() = default;

    AccessLogDatagram(std::chrono::system_clock::time_point _timestamp,
                      http_method_t _method, const char *_uri,
                      const char *_remote_host,
                      const char *_host, const char *_site,
                      const char *_referer, const char *_user_agent,
                      http_status_t _status, int64_t _length,
                      uint64_t _traffic_received, uint64_t _traffic_sent,
                      std::chrono::steady_clock::duration _duration) noexcept
        :timestamp(ExportTimestamp(_timestamp)),
         remote_host(_remote_host), host(_host), site(_site),
         forwarded_to(nullptr),
         http_method(_method),
         http_uri(_uri), http_referer(_referer), user_agent(_user_agent),
         message(nullptr),
         http_status(_status),
         length(_length),
         traffic_received(_traffic_received), traffic_sent(_traffic_sent),
         duration(ExportDuration(_duration)),
         valid_timestamp(true),
         valid_http_method(true), valid_http_status(true),
         valid_length(_length >= 0), valid_traffic(true),
         valid_duration(true) {}

    explicit AccessLogDatagram(StringView _message) noexcept
        :remote_host(nullptr), host(nullptr), site(nullptr),
         forwarded_to(nullptr),
         http_uri(nullptr), http_referer(nullptr), user_agent(nullptr),
         message(_message),
         valid_timestamp(false),
         valid_http_method(false), valid_http_status(false),
         valid_length(false), valid_traffic(false),
         valid_duration(false) {}

    void SetTimestamp(std::chrono::system_clock::time_point t) noexcept {
        timestamp = ExportTimestamp(t);
        valid_timestamp = true;
    }

    static constexpr uint64_t ExportTimestamp(std::chrono::system_clock::time_point t) noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(t.time_since_epoch()).count();
    }

    static constexpr uint64_t ExportDuration(std::chrono::steady_clock::duration d) noexcept {
        return std::chrono::duration_cast<std::chrono::microseconds>(d).count();
    }
};

#endif
