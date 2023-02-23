// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ResponseHandler.hxx"
#include "pool/UniquePtr.hxx"
#include "co/Compat.hxx"
#include "util/Cancellable.hxx"

struct PendingResponse;

/**
 * Awaitable #HttpResponseHandler implementation, to be used in
 * coroutines.
 */
class CoHttpResponseHandler
	: protected HttpResponseHandler
{
	struct pool &pool;

	UniquePoolPtr<PendingResponse> response;
	std::exception_ptr error;

	std::coroutine_handle<> continuation;

protected:
		CancellablePointer cancel_ptr;

public:
	explicit CoHttpResponseHandler(struct pool &_pool) noexcept
		:pool(_pool) {}

	~CoHttpResponseHandler() noexcept {
		if (cancel_ptr)
			cancel_ptr.Cancel();
	}

	CoHttpResponseHandler(const CoHttpResponseHandler &) = delete;
	CoHttpResponseHandler &operator=(const CoHttpResponseHandler &) = delete;

	auto operator co_await() noexcept {
		struct Awaitable final {
			CoHttpResponseHandler &request;

			bool await_ready() const noexcept {
				return request.IsReady();
			}

			std::coroutine_handle<> await_suspend(std::coroutine_handle<> _continuation) const noexcept {
				request.continuation = _continuation;
				return std::noop_coroutine();
			}

			UniquePoolPtr<PendingResponse> await_resume() const {
				return request.AwaitResume();
			}
		};

		return Awaitable{*this};
	}

private:
	bool IsReady() const noexcept {
		return !cancel_ptr;
	}

	UniquePoolPtr<PendingResponse> AwaitResume() {
		if (error)
			std::rethrow_exception(std::move(error));

		return std::move(response);
	}

	/* virtual methods from class HttpResponseHandler */
	void OnHttpResponse(HttpStatus status, StringMap &&headers,
			    UnusedIstreamPtr body) noexcept final;
	void OnHttpError(std::exception_ptr error) noexcept final;
};
