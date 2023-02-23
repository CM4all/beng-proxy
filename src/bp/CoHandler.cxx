// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "PendingResponse.hxx"
#include "co/Task.hxx"

#include <cassert>

inline Co::InvokeTask
Request::CoRun(Co::Task<PendingResponse> task)
{
	co_response = UniquePoolPtr<PendingResponse>::Make(pool, co_await task);

	assert(co_response);
}

void
Request::CoStart(Co::Task<PendingResponse> task,
		 BoundMethod<void(std::exception_ptr) noexcept> on_completion) noexcept
{
	assert(!co_handler);

	co_handler = CoRun(std::move(task));
	co_handler.Start(on_completion);
}

void
Request::CoStart(Co::Task<PendingResponse> task) noexcept
{
	CoStart(std::move(task), BIND_THIS_METHOD(OnCoCompletion));
}

void
Request::OnCoCompletion(std::exception_ptr error) noexcept
{
	assert(error || co_response);

	if (error)
		LogDispatchError(error);
	else
		DispatchResponse(std::move(co_response));
}
