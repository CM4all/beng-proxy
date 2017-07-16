/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_LOG_GLUE_HXX
#define BENG_PROXY_LOG_GLUE_HXX

#include <http/method.h>
#include <http/status.h>

#include <stdint.h>

struct UidGid;

void
log_global_init(const char *program, const UidGid *user);

void
log_global_deinit(void);

bool
log_global_enabled(void);

/**
 * @param length the number of response body (payload) bytes sent
 * to our HTTP client or negative if there was no response body
 * (which is different from "empty response body")
 * @param traffic_received the number of raw bytes received from our
 * HTTP client
 * @param traffic_sent the number of raw bytes sent to our HTTP client
 * (which includes status line, headers and transport encoding
 * overhead such as chunk headers)
 */
bool
log_http_request(uint64_t timestamp, http_method_t method, const char *uri,
                 const char *remote_host, const char *host, const char *site,
                 const char *referer, const char *user_agent,
                 http_status_t status, int64_t length,
                 uint64_t traffic_received, uint64_t traffic_sent,
                 uint64_t duration);

#endif
