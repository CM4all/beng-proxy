// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CoResponseHandler.hxx"
#include "bp/PendingResponse.hxx"

void
CoHttpResponseHandler::OnHttpResponse(HttpStatus status,
				      StringMap &&headers,
				      UnusedIstreamPtr body) noexcept
{
	response = UniquePoolPtr<PendingResponse>::Make(pool, status,
							std::move(headers),
							UnusedHoldIstreamPtr{pool, std::move(body)});
	cancel_ptr = nullptr;

	if (continuation)
		continuation.resume();
}

void
CoHttpResponseHandler::OnHttpError(std::exception_ptr _error) noexcept
{
	error = std::move(_error);
	cancel_ptr = nullptr;

	if (continuation)
		continuation.resume();
}
