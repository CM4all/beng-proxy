/*
 * Copyright 2007-2021 CM4all GmbH
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

#pragma once

#include "lib/curl/Global.hxx"
#include "lib/curl/Headers.hxx"
#include "http/Method.h"
#include "http/Status.h"

#include <string>

template<typename T> struct ConstBuffer;
class EventLoop;

struct GlueHttpResponse {
	http_status_t status;

	Curl::Headers headers;

	std::string body;

	GlueHttpResponse(http_status_t _status,
			 Curl::Headers &&_headers,
			 std::string &&_body)
		:status(_status), headers(std::move(_headers)), body(_body) {}
};

class GlueHttpClient {
	CurlGlobal curl_global;

	bool verbose = false;

public:
	explicit GlueHttpClient(EventLoop &event_loop);
	~GlueHttpClient();

	GlueHttpClient(const GlueHttpClient &) = delete;
	GlueHttpClient &operator=(const GlueHttpClient &) = delete;

	void EnableVerbose() noexcept {
		verbose = true;
	}

	GlueHttpResponse Request(EventLoop &event_loop,
				 http_method_t method, const char *uri,
				 ConstBuffer<void> body);
};
