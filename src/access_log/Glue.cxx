/*
 * Glue code for the logging protocol.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Glue.hxx"
#include "Config.hxx"
#include "Launch.hxx"
#include "Client.hxx"
#include "Datagram.hxx"
#include "OneLine.hxx"
#include "http_server/Request.hxx"

#include <assert.h>
#include <string.h>

static bool global_log_enabled;
static LogClient *global_log_client;

void
log_global_init(const AccessLogConfig &config, const UidGid *user)
{
    assert(global_log_client == nullptr);

    if (config.command.empty() || config.command == "internal") {
        global_log_enabled = false;
        return;
    }

    if (config.command == "null") {
        global_log_enabled = true;
        return;
    }

    auto lp = log_launch(config.command.c_str(), user);
    assert(lp.fd.IsDefined());

    global_log_client = new LogClient(std::move(lp.fd));
    global_log_enabled = true;
}

void
log_global_deinit(void)
{
    delete global_log_client;
}

static void
log_http_request(const AccessLogDatagram &d)
{
    if (!global_log_enabled)
        LogOneLine(d);
    else if (global_log_client != nullptr)
        global_log_client->Send(d);
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

    if (global_log_enabled && global_log_client == nullptr)
        return;

    const AccessLogDatagram d(std::chrono::system_clock::now(),
                              request->method, request->uri,
                              request->remote_host,
                              request->headers.Get("host"),
                              site,
                              referer, user_agent,
                              status, content_length,
                              bytes_received, bytes_sent,
                              duration);
    log_http_request(d);
}
