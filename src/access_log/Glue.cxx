/*
 * Copyright 2007-2017 Content Management AG
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Glue.hxx"
#include "Launch.hxx"
#include "Client.hxx"
#include "net/ConnectSocket.hxx"
#include "net/log/Datagram.hxx"
#include "net/log/OneLine.hxx"
#include "http_server/Request.hxx"
#include "system/Error.hxx"

#include <assert.h>
#include <string.h>

AccessLogGlue::AccessLogGlue(const AccessLogConfig &_config,
                             std::unique_ptr<LogClient> _client) noexcept
    :config(_config), client(std::move(_client)) {}

AccessLogGlue::~AccessLogGlue() noexcept = default;

AccessLogGlue *
AccessLogGlue::Create(const AccessLogConfig &config,
                      const UidGid *user)
{
    switch (config.type) {
    case AccessLogConfig::Type::DISABLED:
        return nullptr;

    case AccessLogConfig::Type::INTERNAL:
        return new AccessLogGlue(config, nullptr);

    case AccessLogConfig::Type::SEND:
        return new AccessLogGlue(config,
                                 std::make_unique<LogClient>(CreateConnectDatagramSocket(config.send_to)));

    case AccessLogConfig::Type::EXECUTE:
        {
            auto lp = LaunchLogger(config.command.c_str(), user);
            assert(lp.fd.IsDefined());

            return new AccessLogGlue(config,
                                     std::make_unique<LogClient>(std::move(lp.fd)));
        }
    }

    assert(false);
    gcc_unreachable();
}

void
AccessLogGlue::Log(const Net::Log::Datagram &d) noexcept
{
    if (!config.ignore_localhost_200.empty() &&
        d.http_uri != nullptr &&
        d.http_uri == config.ignore_localhost_200 &&
        d.host != nullptr &&
        strcmp(d.host, "localhost") == 0 &&
        d.http_status == HTTP_STATUS_OK)
        return;

    if (client != nullptr)
        client->Send(d);
    else
        LogOneLine(FileDescriptor(STDOUT_FILENO), d);
}

/**
 * Extract the right-most item of a comma-separated list, such as an
 * X-Forwarded-For header value.  Returns the remaining string and the
 * right-most item as a std::pair.
 */
gcc_pure
static std::pair<StringView, StringView>
LastListItem(StringView list) noexcept
{
    const char *comma = (const char *)memrchr(list.data, ',', list.size);
    if (comma == nullptr) {
        list.Strip();
        if (list.empty())
            return std::make_pair(nullptr, nullptr);

        return std::make_pair("", list);
    }

    StringView value = list;
    value.MoveFront(comma + 1);
    value.Strip();

    list.size = comma - list.data;

    return std::make_pair(list, value);
}

/**
 * Extract the "real" remote host from an X-Forwarded-For request header.
 *
 * @param trust a list of trusted proxies
 */
gcc_pure
static StringView
GetRealRemoteHost(const char *xff, const std::set<std::string> &trust) noexcept
{
    StringView list(xff);
    StringView result(nullptr);

    while (true) {
        auto l = LastListItem(list);
        if (l.second.empty())
            /* list finished; return the last good address (even if
               it's a trusted proxy) */
            return result;

        result = l.second;
        if (trust.find(std::string(result.data, result.size)) == trust.end())
            /* this address is not a trusted proxy; return it */
            return result;

        list = l.first;
    }
}

void
AccessLogGlue::Log(std::chrono::system_clock::time_point now,
                   const HttpServerRequest &request, const char *site,
                   const char *forwarded_to,
                   const char *host, const char *x_forwarded_for,
                   const char *referer, const char *user_agent,
                   http_status_t status, int64_t content_length,
                   uint64_t bytes_received, uint64_t bytes_sent,
                   std::chrono::steady_clock::duration duration) noexcept
{
    assert(http_method_is_valid(request.method));
    assert(http_status_is_valid(status));

    const char *remote_host = request.remote_host;
    std::string buffer;

    if (remote_host != nullptr &&
        !config.trust_xff.empty() &&
        config.trust_xff.find(remote_host) != config.trust_xff.end() &&
        x_forwarded_for != nullptr) {
        auto r = GetRealRemoteHost(x_forwarded_for, config.trust_xff);
        if (r != nullptr) {
            buffer.assign(r.data, r.size);
            remote_host = buffer.c_str();
        }
    }

    Net::Log::Datagram d(Net::Log::FromSystem(now),
                         request.method, request.uri,
                         remote_host,
                         host,
                         site,
                         referer, user_agent,
                         status, content_length,
                         bytes_received, bytes_sent,
                         std::chrono::duration_cast<Net::Log::Duration>(duration));
    d.forwarded_to = forwarded_to;

    Log(d);
}

void
AccessLogGlue::Log(std::chrono::system_clock::time_point now,
                   const HttpServerRequest &request, const char *site,
                   const char *forwarded_to,
                   const char *referer, const char *user_agent,
                   http_status_t status, int64_t content_length,
                   uint64_t bytes_received, uint64_t bytes_sent,
                   std::chrono::steady_clock::duration duration) noexcept
{
    Log(now, request, site, forwarded_to,
        request.headers.Get("host"),
        request.headers.Get("x-forwarded-for"),
        referer, user_agent,
        status, content_length,
        bytes_received, bytes_sent,
        duration);
}

SocketDescriptor
AccessLogGlue::GetChildSocket() noexcept
{
    return config.forward_child_errors && client
        ? client->GetSocket()
        : SocketDescriptor::Undefined();
}
