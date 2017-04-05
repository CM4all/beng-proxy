/*
 * Helpers for implementing HTTP "Upgrade".
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "http_upgrade.hxx"
#include "http_headers.hxx"
#include "strmap.hxx"
#include "http/List.hxx"

const char *const http_upgrade_request_headers[] = {
    "connection",
    "upgrade",
    "origin",
    "sec-websocket-key",
    "sec-websocket-protocol",
    "sec-websocket-version",
    nullptr,
};

const char *const http_upgrade_response_headers[] = {
    "connection",
    "upgrade",
    "sec-websocket-accept",
    nullptr,
};

bool
http_is_upgrade(const char *connection)
{
    assert(connection != nullptr);

    return http_list_contains_i(connection, "upgrade");
}

bool
http_is_upgrade(const StringMap &headers)
{
    const char *value = headers.Get("connection");
    return value != nullptr && http_is_upgrade(value);
}

bool
http_is_upgrade(const HttpHeaders &headers)
{
    return http_is_upgrade(headers.GetMap());
}
