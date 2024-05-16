// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "ResponseHandler.hxx"
#include "CommonHeaders.hxx"
#include "istream/istream_string.hxx"
#include "istream/UnusedPtr.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

void
HttpResponseHandler::InvokeResponse(struct pool &pool,
				    HttpStatus status,
				    const char *msg) noexcept
{
	assert(http_status_is_valid(status));
	assert(msg != nullptr);

	StringMap headers;
	headers.Add(pool, content_type_header, "text/plain; charset=utf-8");
	InvokeResponse(status, std::move(headers),
		       istream_string_new(pool, msg));
}
