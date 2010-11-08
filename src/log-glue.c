/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "log-glue.h"
#include "log-launch.h"
#include "log-client.h"

#include <assert.h>

static struct log_client *global_log_client;

bool
log_global_init(const char *program)
{
    assert(global_log_client == NULL);

    if (program == NULL)
        return true;

    struct log_process lp;
    if (!log_launch(&lp, program))
        return false;

    assert(lp.fd >= 0);

    global_log_client = log_client_new(lp.fd);
    assert(global_log_client != NULL);

    return true;
}

void
log_global_deinit(void)
{
    if (global_log_client != NULL)
        log_client_free(global_log_client);
}

bool
log_global_enabled(void)
{
    return global_log_client != NULL;
}

bool
log_http_request(uint64_t timestamp, http_method_t method, const char *uri,
                 const char *site, const char *referer, const char *user_agent,
                 http_status_t status, uint64_t length)
{
    assert(http_method_is_valid(method));
    assert(uri != NULL);
    assert(http_status_is_valid(status));

    if (global_log_client == NULL)
        return true;

    log_client_begin(global_log_client);
    log_client_append_u64(global_log_client, LOG_TIMESTAMP, timestamp);
    if (site != NULL)
        log_client_append_string(global_log_client, LOG_SITE, site);
    log_client_append_u8(global_log_client, LOG_HTTP_METHOD, method);
    log_client_append_string(global_log_client, LOG_HTTP_URI, uri);
    if (referer != NULL)
        log_client_append_string(global_log_client, LOG_HTTP_REFERER, referer);
    if (user_agent != NULL)
        log_client_append_string(global_log_client, LOG_USER_AGENT,
                                 user_agent);
    log_client_append_u16(global_log_client, LOG_HTTP_STATUS, status);
    log_client_append_u64(global_log_client, LOG_LENGTH, length);

    return log_client_commit(global_log_client);
}
