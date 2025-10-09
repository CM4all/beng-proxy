// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "RLogger.hxx"
#include "Instance.hxx"
#include "access_log/Glue.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"
#include "AllocatorPtr.hxx"

[[gnu::pure]]
static const char *
GetRealRemoteHost(const AccessLogGlue *access_logger,
		  const IncomingHttpRequest &request) noexcept
{
	const char *remote_host = request.remote_host;

	if (access_logger != nullptr) {
		const char *x_forwarded_for = request.headers.Get(x_forwarded_for_header);

		const auto &config = access_logger->GetXForwardedForConfig();
		if (const auto r = config.GetRealRemoteHost(remote_host,
							    request.remote_address,
							    x_forwarded_for);
		    !r.empty()) {
			AllocatorPtr alloc{request.pool};
			remote_host = alloc.DupZ(r);
		}
	}

	return remote_host;
}

LbRequestLogger::LbRequestLogger(LbInstance &_instance,
				 HttpStats &_http_stats,
				 AccessLogGlue *_access_logger,
				 bool _access_logger_only_errors,
				 const IncomingHttpRequest &request) noexcept
	:IncomingHttpRequestLogger(_access_logger != nullptr && !_access_logger_only_errors),
	 instance(_instance), http_stats(_http_stats),
	 access_logger(_access_logger),
	 clock(instance.event_loop.SteadyNow()),
	 host(request.headers.Get(host_header)),
	 real_remote_host(::GetRealRemoteHost(_access_logger, request)),
	 referer(request.headers.Get(referer_header)),
	 user_agent(request.headers.Get(user_agent_header)),
	 access_logger_only_errors(_access_logger_only_errors)
{
}

void
LbRequestLogger::LogHttpRequest(IncomingHttpRequest &request,
				Event::Duration wait_duration,
				HttpStatus status,
				Net::Log::ContentType content_type,
				int_least64_t length,
				uint_least64_t bytes_received, uint_least64_t bytes_sent) noexcept
{
	const auto duration = clock.GetDuration(instance.event_loop.SteadyNow(), wait_duration);

	instance.http_stats.AddRequest(status,
				       bytes_received, bytes_sent,
				       duration);
	http_stats.AddRequest(status,
			      bytes_received, bytes_sent,
			      duration);

	if (access_logger != nullptr &&
	    (!access_logger_only_errors || http_status_is_error(status)))
		access_logger->Log(instance.event_loop.SystemNow(),
				   request, site_name,
				   analytics_id,
				   generator != nullptr && *generator != 0 ? generator : nullptr,
				   forwarded_to,
				   host,
				   real_remote_host, nullptr,
				   referer,
				   user_agent,
				   status, content_type, length,
				   bytes_received, bytes_sent,
				   duration);
}
