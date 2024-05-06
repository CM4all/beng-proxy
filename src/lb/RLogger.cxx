// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "RLogger.hxx"
#include "Instance.hxx"
#include "access_log/Glue.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"

LbRequestLogger::LbRequestLogger(LbInstance &_instance,
				 HttpStats &_http_stats,
				 bool _access_logger,
				 bool _access_logger_only_errors,
				 const IncomingHttpRequest &request) noexcept
	:instance(_instance), http_stats(_http_stats),
	 start_time(instance.event_loop.SteadyNow()),
	 host(request.headers.Get(host_header)),
	 x_forwarded_for(request.headers.Get(x_forwarded_for_header)),
	 referer(request.headers.Get(referer_header)),
	 user_agent(request.headers.Get(user_agent_header)),
	 access_logger(_access_logger),
	 access_logger_only_errors(_access_logger_only_errors)
{
}

void
LbRequestLogger::LogHttpRequest(IncomingHttpRequest &request,
				HttpStatus status, int64_t length,
				uint64_t bytes_received, uint64_t bytes_sent) noexcept
{
	const auto duration = GetDuration(instance.event_loop.SteadyNow());

	instance.http_stats.AddRequest(status,
				       bytes_received, bytes_sent,
				       duration);
	http_stats.AddRequest(status,
			      bytes_received, bytes_sent,
			      duration);

	if (access_logger && instance.access_log != nullptr &&
	    (!access_logger_only_errors || http_status_is_error(status)))
		instance.access_log->Log(instance.event_loop.SystemNow(),
					 request, site_name,
					 analytics_id,
					 generator != nullptr && *generator != 0 ? generator : nullptr,
					 forwarded_to,
					 host,
					 x_forwarded_for,
					 referer,
					 user_agent,
					 status, length,
					 bytes_received, bytes_sent,
					 duration);
}
