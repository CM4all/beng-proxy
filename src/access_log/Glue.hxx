/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_GLUE_HXX
#define BENG_PROXY_LOG_GLUE_HXX

#include <http/method.h>
#include <http/status.h>

#include <chrono>

#include <stdint.h>

struct UidGid;
struct AccessLogConfig;
struct AccessLogDatagram;
struct HttpServerRequest;

void
log_global_init(const AccessLogConfig &config, const UidGid *user);

void
log_global_deinit(void);

#ifdef NO_ACCESS_LOG

static inline void
access_log(gcc_unused HttpServerRequest *request,
           gcc_unused const char *site,
           gcc_unused const char *referer,
           gcc_unused const char *user_agent,
           gcc_unused http_status_t status, gcc_unused int64_t length,
           gcc_unused uint64_t bytes_received,
           gcc_unused uint64_t bytes_sent,
           gcc_unused std::chrono::steady_clock::duration duration)
{
}

#else

/**
 * @param length the number of response body (payload) bytes sent
 * to our HTTP client or negative if there was no response body
 * (which is different from "empty response body")
 * @param bytes_received the number of raw bytes received from our
 * HTTP client
 * @param bytes_sent the number of raw bytes sent to our HTTP client
 * (which includes status line, headers and transport encoding
 * overhead such as chunk headers)
 */
void
access_log(HttpServerRequest *request, const char *site,
           const char *referer, const char *user_agent,
           http_status_t status, int64_t length,
           uint64_t bytes_received, uint64_t bytes_sent,
           std::chrono::steady_clock::duration duration);

#endif

#endif
