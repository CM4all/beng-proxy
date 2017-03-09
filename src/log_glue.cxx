/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log_glue.hxx"

extern "C" {
#include "log-launch.h"
#include "log-client.h"
}

#include <glib.h>

#include <assert.h>
#include <string.h>

static bool global_log_enabled;
static struct log_client *global_log_client;

bool
log_global_init(const char *program, const struct daemon_user *user)
{
    assert(global_log_client == nullptr);

    if (program == nullptr || *program == 0 || strcmp(program, "internal") == 0) {
        global_log_enabled = false;
        return true;
    }

    if (strcmp(program, "null") == 0) {
        global_log_enabled = true;
        return true;
    }

    struct log_process lp;
    if (!log_launch(&lp, program, user))
        return false;

    assert(lp.fd >= 0);

    global_log_client = log_client_new(lp.fd);
    assert(global_log_client != nullptr);
    global_log_enabled = global_log_client != nullptr;

    return true;
}

void
log_global_deinit(void)
{
    if (global_log_client != nullptr)
        log_client_free(global_log_client);
}

bool
log_global_enabled(void)
{
    return global_log_enabled;
}

bool
log_http_request(uint64_t timestamp, http_method_t method, const char *uri,
                 const char *remote_host, const char *site,
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

    log_client_begin(global_log_client);
    log_client_append_u64(global_log_client, LOG_TIMESTAMP, timestamp);
    if (remote_host != nullptr)
        log_client_append_string(global_log_client, LOG_REMOTE_HOST,
                                 remote_host);
    if (site != nullptr)
        log_client_append_string(global_log_client, LOG_SITE, site);
    log_client_append_u8(global_log_client, LOG_HTTP_METHOD, method);
    log_client_append_string(global_log_client, LOG_HTTP_URI, uri);
    if (referer != nullptr)
        log_client_append_string(global_log_client, LOG_HTTP_REFERER, referer);
    if (user_agent != nullptr)
        log_client_append_string(global_log_client, LOG_USER_AGENT,
                                 user_agent);
    log_client_append_u16(global_log_client, LOG_HTTP_STATUS, status);

    if (length >= 0)
        log_client_append_u64(global_log_client, LOG_LENGTH, length);

    struct {
        uint64_t received, sent;
    } traffic = {
        .received = GUINT64_TO_BE(traffic_received),
        .sent = GUINT64_TO_BE(traffic_sent),
    };

    log_client_append_attribute(global_log_client, LOG_TRAFFIC,
                                &traffic, sizeof(traffic));

    if (duration > 0)
        log_client_append_u64(global_log_client, LOG_DURATION, duration);

    return log_client_commit(global_log_client);
}
