/*
 * Copyright 2007-2022 CM4all GmbH
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
#include "http/IncomingRequest.hxx"

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

void
AccessLogGlue::Log(std::chrono::system_clock::time_point now,
		   const IncomingHttpRequest &request, const char *site,
		   const char *forwarded_to,
		   const char *host, const char *x_forwarded_for,
		   const char *referer, const char *user_agent,
		   http_status_t status, int64_t content_length,
		   uint64_t bytes_received, uint64_t bytes_sent,
		   std::chrono::steady_clock::duration duration) noexcept
{
	assert(http_method_is_valid(request.method));
	assert(status == http_status_t{} || http_status_is_valid(status));

	const char *remote_host = request.remote_host;
	std::string buffer;

	if (remote_host != nullptr && x_forwarded_for != nullptr &&
	    config.xff.IsTrustedHost(remote_host)) {
		auto r = config.xff.GetRealRemoteHost(x_forwarded_for);
		if (!r.empty()) {
			buffer.assign(r);
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
		   const IncomingHttpRequest &request, const char *site,
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
