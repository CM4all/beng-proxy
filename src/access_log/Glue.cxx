/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Glue.hxx"
#include "Launch.hxx"
#include "Client.hxx"
#include "util/ByteOrder.hxx"

#include <assert.h>
#include <string.h>

static bool global_log_enabled;
static LogClient *global_log_client;

void
log_global_init(const char *program, const struct daemon_user *user)
{
    assert(global_log_client == nullptr);

    if (program == nullptr || *program == 0 || strcmp(program, "internal") == 0) {
        global_log_enabled = false;
        return;
    }

    if (strcmp(program, "null") == 0) {
        global_log_enabled = true;
        return;
    }

    auto lp = log_launch(program, user);
    assert(lp.fd.IsDefined());

    global_log_client = new LogClient(std::move(lp.fd));
    global_log_enabled = true;
}

void
log_global_deinit(void)
{
    delete global_log_client;
}

bool
log_global_enabled(void)
{
    return global_log_enabled;
}

bool
log_http_request(uint64_t timestamp, http_method_t method, const char *uri,
                 const char *remote_host, const char *host, const char *site,
                 const char *referer, const char *user_agent,
                 http_status_t status, int64_t length,
                 uint64_t traffic_received, uint64_t traffic_sent,
                 uint64_t duration)
{
    assert(http_method_is_valid(method));
    assert(uri != nullptr);
    assert(http_status_is_valid(status));

    if (global_log_client == nullptr)
        return true;

    auto &client = *global_log_client;
    client.Begin();
    client.AppendU64(LOG_TIMESTAMP, timestamp);
    if (remote_host != nullptr)
        client.AppendString(LOG_REMOTE_HOST, remote_host);
    if (host != nullptr)
        client.AppendString(LOG_HOST, host);
    if (site != nullptr)
        client.AppendString(LOG_SITE, site);
    client.AppendU8(LOG_HTTP_METHOD, method);
    client.AppendString(LOG_HTTP_URI, uri);
    if (referer != nullptr)
        client.AppendString(LOG_HTTP_REFERER, referer);
    if (user_agent != nullptr)
        client.AppendString(LOG_USER_AGENT, user_agent);
    client.AppendU16(LOG_HTTP_STATUS, status);

    if (length >= 0)
        client.AppendU64(LOG_LENGTH, length);

    struct {
        uint64_t received, sent;
    } traffic = {
        .received = ToBE64(traffic_received),
        .sent = ToBE64(traffic_sent),
    };

    client.AppendAttribute(LOG_TRAFFIC, &traffic, sizeof(traffic));

    if (duration > 0)
        client.AppendU64(LOG_DURATION, duration);

    return client.Commit();
}
