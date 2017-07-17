/*
 * Access logging.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "access_log.hxx"

#ifndef NO_ACCESS_LOG

#include "http_server/Request.hxx"
#include "access_log/OneLine.hxx"
#include "access_log/Glue.hxx"
#include "access_log/Datagram.hxx"

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

    const AccessLogDatagram d(std::chrono::system_clock::now(),
                              request->method, request->uri,
                              request->remote_host,
                              request->headers.Get("host"),
                              site,
                              referer, user_agent,
                              status, content_length,
                              bytes_received, bytes_sent,
                              duration);

    if (log_global_enabled()) {
        log_http_request(d);
        return;
    }

    LogOneLine(d);
}

#endif
