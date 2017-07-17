/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Glue.hxx"
#include "Launch.hxx"
#include "Client.hxx"
#include "Datagram.hxx"

#include <assert.h>
#include <string.h>

static bool global_log_enabled;
static LogClient *global_log_client;

void
log_global_init(const char *program, const UidGid *user)
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
log_http_request(std::chrono::system_clock::time_point timestamp,
                 http_method_t method, const char *uri,
                 const char *remote_host, const char *host, const char *site,
                 const char *referer, const char *user_agent,
                 http_status_t status, int64_t length,
                 uint64_t traffic_received, uint64_t traffic_sent,
                 std::chrono::steady_clock::duration duration)
{
    assert(http_method_is_valid(method));
    assert(uri != nullptr);
    assert(http_status_is_valid(status));

    if (global_log_client == nullptr)
        return true;

    const AccessLogDatagram d(timestamp, method, uri,
                              remote_host, host, site,
                              referer, user_agent,
                              status, length,
                              traffic_received, traffic_sent,
                              duration);
    return global_log_client->Send(d);
}
