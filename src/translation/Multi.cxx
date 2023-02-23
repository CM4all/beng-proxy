// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Multi.hxx"
#include "pool/LeakDetector.hxx"
#include "translation/Response.hxx"
#include "translation/Handler.hxx"
#include "util/Cancellable.hxx"
#include "AllocatorPtr.hxx"

class MultiTranslationService::Request final
	: PoolLeakDetector, TranslateHandler, Cancellable {

	const AllocatorPtr alloc;
	const TranslateRequest &request;
	const StopwatchPtr &parent_stopwatch;
	TranslateHandler &handler;

	List::const_iterator i;
	const List::const_iterator end;

	CancellablePointer cancel_ptr;

public:
	Request(AllocatorPtr _alloc, const TranslateRequest &_request,
		const StopwatchPtr &_parent_stopwatch,
		TranslateHandler &_handler,
		CancellablePointer &caller_cancel_ptr,
		List::const_iterator _begin,
		List::const_iterator _end) noexcept
		:PoolLeakDetector(_alloc), alloc(_alloc),
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
		(*i)->SendRequest(alloc, request, parent_stopwatch,
				  *this, cancel_ptr);
	}

private:
	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		cancel_ptr.Cancel();
		Destroy();
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override;

	void OnTranslateError(std::exception_ptr error) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.OnTranslateError(std::move(error));
	}
};

void
MultiTranslationService::Request::OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept
{
	if (response->defer && ++i != end) {
		/* consult the next translation server (if there is
		   one) */
		Start();
	} else {
		auto &_handler = handler;
		Destroy();
		_handler.OnTranslateResponse(std::move(response));
	}
}

void
MultiTranslationService::SendRequest(AllocatorPtr alloc,
				     const TranslateRequest &request,
				     const StopwatchPtr &parent_stopwatch,
				     TranslateHandler &handler,
				     CancellablePointer &cancel_ptr) noexcept
{
	assert(!items.empty());

	if (items.size() == 1) {
		items.front()->SendRequest(alloc, request, parent_stopwatch,
					   handler, cancel_ptr);
		return;
	}

	auto *r = alloc.New<Request>(alloc, request, parent_stopwatch,
				     handler, cancel_ptr,
				     items.begin(), items.end());
	r->Start();
}
