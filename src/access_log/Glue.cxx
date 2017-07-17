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

AccessLogGlue::~AccessLogGlue()
{
    delete client;
}

AccessLogGlue *
AccessLogGlue::Create(const AccessLogConfig &config,
                      const UidGid *user)
{
    switch (config.type) {
    case AccessLogConfig::Type::DISABLED:
        return nullptr;

    case AccessLogConfig::Type::INTERNAL:
        return new AccessLogGlue(nullptr);

    case AccessLogConfig::Type::EXECUTE:
        {
            auto lp = log_launch(config.command.c_str(), user);
            assert(lp.fd.IsDefined());

            return new AccessLogGlue(new LogClient(std::move(lp.fd)));
        }
    }

    assert(false);
    gcc_unreachable();
}

void
AccessLogGlue::Log(const AccessLogDatagram &d)
{
    if (client != nullptr)
        client->Send(d);
    else
        LogOneLine(d);
}

void
AccessLogGlue::Log(HttpServerRequest &request, const char *site,
                   const char *referer, const char *user_agent,
                   http_status_t status, int64_t content_length,
                   uint64_t bytes_received, uint64_t bytes_sent,
                   std::chrono::steady_clock::duration duration)
{
    assert(http_method_is_valid(request.method));
    assert(http_status_is_valid(status));

    const AccessLogDatagram d(std::chrono::system_clock::now(),
                              request.method, request.uri,
                              request.remote_host,
                              request.headers.Get("host"),
                              site,
                              referer, user_agent,
                              status, content_length,
                              bytes_received, bytes_sent,
                              duration);
    Log(d);
}
