/*
 * Access logging.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "access_log.hxx"

#ifndef NO_ACCESS_LOG

#include "http_server/Request.hxx"
#include "access_log/Glue.hxx"

#include <stdio.h>
#include <time.h>

static const char *
optional_string(const char *p)
{
    if (p == nullptr)
        return "-";

    return p;
}

void
access_log(HttpServerRequest *request, const char *site,
           const char *referer, const char *user_agent,
           http_status_t status, int64_t content_length,
           uint64_t bytes_received, uint64_t bytes_sent,
           std::chrono::steady_clock::duration duration)
{
    assert(request != nullptr);
    assert(http_method_is_valid(request->method));
    assert(http_status_is_valid(status));

    if (log_global_enabled()) {
        log_http_request(std::chrono::system_clock::now(),
                         request->method, request->uri,
                         request->remote_host,
                         request->headers.Get("host"),
                         site,
                         referer, user_agent,
                         status, content_length,
                         bytes_received, bytes_sent,
                         duration);
        return;
    }

    time_t now = time(nullptr);
    char stamp[32];

    if (site == nullptr)
        site = "-";

    strftime(stamp, sizeof(stamp),
             "%d/%b/%Y:%H:%M:%S %z", localtime(&now));

    char length_buffer[32];
    const char *length;
    if (content_length < 0)
        length = "-";
    else {
        snprintf(length_buffer, sizeof(length_buffer), "%llu",
                 (unsigned long long)content_length);
        length = length_buffer;
    }

    const auto duration_us = std::chrono::duration_cast<std::chrono::microseconds>(duration);

    printf("%s %s - - [%s] \"%s %s HTTP/1.1\" %u %s \"%s\" \"%s\" %llu\n",
           site, optional_string(request->remote_host), stamp,
           http_method_to_string(request->method),
           request->uri,
           status, length,
           optional_string(referer),
           optional_string(user_agent),
           (unsigned long long)duration_us.count());
}

#endif
