// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "RLogger.hxx"
#include "Instance.hxx"
#include "access_log/Glue.hxx"
#include "http/CommonHeaders.hxx"
#include "http/IncomingRequest.hxx"

BpRequestLogger::BpRequestLogger(BpInstance &_instance,
				 TaggedHttpStats &_http_stats,
				 bool _access_logger,
				 bool _access_logger_only_errors) noexcept
	:instance(_instance), http_stats(_http_stats),
	 start_time(instance.event_loop.SteadyNow()),
	 access_logger(_access_logger),
	 access_logger_only_errors(_access_logger_only_errors)
{
}

void
BpRequestLogger::LogHttpRequest(IncomingHttpRequest &request,
				HttpStatus status, int64_t length,
				uint64_t bytes_received, uint64_t bytes_sent) noexcept
{
	const auto duration = GetDuration(instance.event_loop.SteadyNow());

	instance.http_stats.AddRequest(status,
				       bytes_received, bytes_sent,
				       duration);

	http_stats.AddRequest(stats_tag, status,
			      bytes_received, bytes_sent,
			      duration);

	if (access_logger && instance.access_log != nullptr &&
	    (!access_logger_only_errors || http_status_is_error(status)))
		instance.access_log->Log(instance.event_loop.SystemNow(),
					 request, site_name,
					 analytics_id,
					 generator != nullptr && *generator != 0 ? generator : nullptr,
					 nullptr,
					 request.headers.Get(referer_header),
					 request.headers.Get(user_agent_header),
					 status, length,
					 bytes_received, bytes_sent,
					 duration);
}
