// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Request.hxx"
#include "http/Headers.hxx"

#include <assert.h>

void
Request::DispatchMethodNotAllowed(const char *allow) noexcept
{
	assert(allow != nullptr);

	HttpHeaders headers;
	headers.Write("content-type", "text/plain");
	headers.Write("allow", allow);

	DispatchError(HttpStatus::METHOD_NOT_ALLOWED,
		      std::move(headers),
		      "This method is not allowed.");
}
