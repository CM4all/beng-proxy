// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "CoTranslate.hxx"
#include "Service.hxx"
#include "AllocatorPtr.hxx"

CoTranslate::CoTranslate(TranslationService &service,
			 AllocatorPtr alloc,
			 const TranslateRequest &request,
			 const StopwatchPtr &parent_stopwatch) noexcept
{
	service.SendRequest(alloc, request, parent_stopwatch, *this,
			    cancel_ptr);
}

void
CoTranslate::OnTranslateResponse(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	response = std::move(_response);
	cancel_ptr = nullptr;

	if (continuation)
		continuation.resume();
}

void
CoTranslate::OnTranslateError(std::exception_ptr _error) noexcept
{
	error = std::move(_error);
	cancel_ptr = nullptr;

	if (continuation)
		continuation.resume();
}
