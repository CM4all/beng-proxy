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

#include "Multi.hxx"
#include "pool/pool.hxx"
#include "pool/LeakDetector.hxx"
#include "translation/Response.hxx"
#include "translation/Handler.hxx"
#include "util/Cancellable.hxx"

class MultiTranslationService::Request final
	: PoolLeakDetector, TranslateHandler, Cancellable {

	struct pool &pool;
	const TranslateRequest &request;
	const StopwatchPtr &parent_stopwatch;
	TranslateHandler &handler;

	List::const_iterator i;
	const List::const_iterator end;

	CancellablePointer cancel_ptr;

public:
	Request(struct pool &_pool, const TranslateRequest &_request,
		const StopwatchPtr &_parent_stopwatch,
		TranslateHandler &_handler,
		CancellablePointer &caller_cancel_ptr,
		List::const_iterator _begin,
		List::const_iterator _end) noexcept
		:PoolLeakDetector(_pool), pool(_pool),
		 request(_request), parent_stopwatch(_parent_stopwatch),
		 handler(_handler),
		 i(_begin), end(_end)
	{
		caller_cancel_ptr = *this;
	}

	void Destroy() noexcept {
		this->~Request();
	}

	void Start() noexcept {
		(*i)->SendRequest(pool, request, parent_stopwatch,
				  *this, cancel_ptr);
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(TranslateResponse &response) noexcept override;

	void OnTranslateError(std::exception_ptr error) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.OnTranslateError(std::move(error));
	}
};

void
MultiTranslationService::Request::OnTranslateResponse(TranslateResponse &response) noexcept
{
	if (response.defer && ++i != end) {
		/* consult the next translation server (if there is
		   one) */
		Start();
	} else {
		auto &_handler = handler;
		Destroy();
		_handler.OnTranslateResponse(response);
	}
}

void
MultiTranslationService::SendRequest(struct pool &pool,
				     const TranslateRequest &request,
				     const StopwatchPtr &parent_stopwatch,
				     TranslateHandler &handler,
				     CancellablePointer &cancel_ptr) noexcept
{
	assert(!items.empty());

	if (items.size() == 1) {
		items.front()->SendRequest(pool, request, parent_stopwatch,
					   handler, cancel_ptr);
		return;
	}

	auto *r = NewFromPool<Request>(pool, pool, request, parent_stopwatch,
				       handler, cancel_ptr,
				       items.begin(), items.end());
	r->Start();
}
