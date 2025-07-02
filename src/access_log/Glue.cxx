// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Glue.hxx"
#include "Launch.hxx"
#include "Client.hxx"
#include "net/ConnectSocket.hxx"
#include "net/log/ContentType.hxx"
#include "net/log/Datagram.hxx"
#include "net/log/OneLine.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Method.hxx"
#include "http/Status.hxx"
#include "util/StringAPI.hxx"

#include <utility> // for std::unreachable()

#include <assert.h>
#include <string.h>

AccessLogGlue::AccessLogGlue(const AccessLogConfig &_config,
			     std::unique_ptr<LogClient> _client) noexcept
	:config(_config), client(std::move(_client)) {}

AccessLogGlue::~AccessLogGlue() noexcept = default;

AccessLogGlue *
AccessLogGlue::Create(EventLoop &event_loop,
		      const AccessLogConfig &config,
		      const UidGid *user)
{
	switch (config.type) {
	case AccessLogConfig::Type::DISABLED:
		return nullptr;

	case AccessLogConfig::Type::INTERNAL:
		return new AccessLogGlue(config, nullptr);

	case AccessLogConfig::Type::SEND:
		return new AccessLogGlue(config,
					 std::make_unique<LogClient>(event_loop, CreateConnectDatagramSocket(config.send_to), config.max_size));

	case AccessLogConfig::Type::EXECUTE:
		{
			auto lp = LaunchLogger(config.command.c_str(), user);
			assert(lp.fd.IsDefined());

			return new AccessLogGlue(config,
						 std::make_unique<LogClient>(event_loop, std::move(lp.fd), config.max_size));
		}
	}

	std::unreachable();
}

void
AccessLogGlue::Log(const Net::Log::Datagram &d) noexcept
{
	if (!config.ignore_localhost_200.empty() &&
	    d.http_uri == config.ignore_localhost_200 &&
	    d.host != nullptr &&
	    StringIsEqual(d.host, "localhost") &&
	    d.http_status == HttpStatus::OK)
		return;

	if (client != nullptr)
		client->Log(d);
	else
		LogOneLine(FileDescriptor(STDOUT_FILENO), d, {});
}

static std::string_view
NullableStringView(const char *s) noexcept
{
	return s != nullptr ? std::string_view{s} : std::string_view{};
}

void
AccessLogGlue::Log(std::chrono::system_clock::time_point now,
		   const IncomingHttpRequest &request, const char *site,
		   const char *analytics_id,
		   const char *generator,
		   const char *forwarded_to,
		   const char *host, const char *x_forwarded_for,
		   const char *referer, const char *user_agent,
		   HttpStatus status,
		   Net::Log::ContentType content_type,
		   int64_t content_length,
		   uint64_t bytes_received, uint64_t bytes_sent,
		   std::chrono::steady_clock::duration duration) noexcept
{
	assert(http_method_is_valid(request.method));
	assert(status == HttpStatus{} || http_status_is_valid(status));

	const char *remote_host = request.remote_host;
	std::string buffer;

	if (x_forwarded_for != nullptr &&
	    ((remote_host != nullptr &&
	      config.xff.IsTrustedHost(remote_host)) ||
	     config.xff.IsTrustedAddress(request.remote_address))) {
		auto r = config.xff.GetRealRemoteHost(x_forwarded_for);
		if (!r.empty()) {
			buffer.assign(r);
			remote_host = buffer.c_str();
		}
	}

	auto d = Net::Log::Datagram{
		.timestamp = Net::Log::FromSystem(now),
		.remote_host = remote_host,
		.host = host,
		.site = site,
		.analytics_id = analytics_id,
		.generator = generator,
		.forwarded_to = forwarded_to,
		.http_uri = NullableStringView(request.uri),
		.http_referer = NullableStringView(referer),
		.user_agent = NullableStringView(user_agent),
		.http_method = request.method,
		.http_status = status,
		.type = Net::Log::Type::HTTP_ACCESS,
		.content_type = content_type,
	}
		.SetTraffic(bytes_received, bytes_sent)
		.SetDuration(std::chrono::duration_cast<Net::Log::Duration>(duration));

	if (content_length >= 0)
		d.SetLength(content_length);

	d.TruncateHttpUri(1024);
	d.TruncateHttpReferer(512);
	d.TruncateUserAgent(64);

	Log(d);
}

void
AccessLogGlue::Log(std::chrono::system_clock::time_point now,
		   const IncomingHttpRequest &request, const char *site,
		   const char *analytics_id,
		   const char *generator,
		   const char *forwarded_to,
		   const char *referer, const char *user_agent,
		   HttpStatus status,
		   Net::Log::ContentType content_type,
		   int64_t content_length,
		   uint64_t bytes_received, uint64_t bytes_sent,
		   std::chrono::steady_clock::duration duration) noexcept
{
	Log(now, request, site, analytics_id, generator,
	    forwarded_to,
	    request.headers.Get(host_header),
	    request.headers.Get(x_forwarded_for_header),
	    referer, user_agent,
	    status, content_type, content_length,
	    bytes_received, bytes_sent,
	    duration);
}

Net::Log::Sink *
AccessLogGlue::GetChildSink() noexcept
{
	return config.forward_child_errors ? client.get() : nullptr;
}
