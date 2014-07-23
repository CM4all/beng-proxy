/*
 * Helpers for implementing HTTP "Upgrade".
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_UPGRADE_HXX
#define BENG_PROXY_HTTP_UPGRADE_HXX

#include <inline/compiler.h>
#include <http/status.h>

class HttpHeaders;

extern const char *const http_upgrade_request_headers[];
extern const char *const http_upgrade_response_headers[];

gcc_pure
static inline bool
http_is_upgrade(http_status_t status)
{
    return status == HTTP_STATUS_SWITCHING_PROTOCOLS;
}

gcc_pure
bool
http_is_upgrade(const char *connection);

gcc_pure
static inline bool
http_is_upgrade(http_status_t status, const char *connection)
{
    return http_is_upgrade(status) && http_is_upgrade(connection);
}

/**
 * Does the header "Connection:Upgrade" exist?
 */
gcc_pure
bool
http_is_upgrade(const struct strmap &headers);

/**
 * Does the header "Connection:Upgrade" exist?
 */
gcc_pure
bool
http_is_upgrade(const HttpHeaders &headers);

gcc_pure
static inline bool
http_is_upgrade(http_status_t status, const struct strmap &headers)
{
    return http_is_upgrade(status) && http_is_upgrade(headers);
}

gcc_pure
static inline bool
http_is_upgrade(http_status_t status, const HttpHeaders &headers)
{
    return http_is_upgrade(status) && http_is_upgrade(headers);
}

#endif
