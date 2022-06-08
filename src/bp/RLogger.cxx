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

#include "RLogger.hxx"
#include "Instance.hxx"
#include "access_log/Glue.hxx"
#include "http/IncomingRequest.hxx"
#include "util/StringView.hxx"

BpRequestLogger::BpRequestLogger(BpInstance &_instance,
				 TaggedHttpStats &_http_stats) noexcept
	:instance(_instance), http_stats(_http_stats),
	 start_time(instance.event_loop.SteadyNow())
{
}

void
BpRequestLogger::LogHttpRequest(IncomingHttpRequest &request,
				http_status_t status, int64_t length,
				uint64_t bytes_received, uint64_t bytes_sent) noexcept
{
	const auto duration = GetDuration(instance.event_loop.SteadyNow());

	instance.http_stats.AddRequest(status,
				       bytes_received, bytes_sent,
				       duration);

	http_stats.AddRequest(StringView{stats_tag}, status,
			      bytes_received, bytes_sent,
			      duration);

	if (instance.access_log != nullptr)
		instance.access_log->Log(instance.event_loop.SystemNow(),
					 request, site_name,
					 nullptr,
					 request.headers.Get("referer"),
					 request.headers.Get("user-agent"),
					 status, length,
					 bytes_received, bytes_sent,
					 duration);
}
