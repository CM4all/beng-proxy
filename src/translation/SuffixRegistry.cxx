// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "SuffixRegistry.hxx"
#include "translation/Service.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "widget/View.hxx"
#include "AllocatorPtr.hxx"

struct SuffixRegistryLookup final : TranslateHandler {
	TranslateRequest request;

	SuffixRegistryHandler &handler;

	SuffixRegistryLookup(std::span<const std::byte> payload,
			     const char *suffix,
			     SuffixRegistryHandler &_handler) noexcept
		:handler(_handler) {
		request.content_type_lookup = payload;
		request.suffix = suffix;
	}

	void Destroy() noexcept {
		this->~SuffixRegistryLookup();
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;
};

void
SuffixRegistryLookup::OnTranslateResponse(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	const auto &response = *_response;
	auto &_handler = handler;
	Destroy();

	_handler.OnSuffixRegistrySuccess(response.content_type,
					 response.auto_gzipped,
					 response.auto_brotli_path,
					 response.auto_brotli,
					 !response.views.empty()
					 ? IntrusiveForwardList<Transformation>{ShallowCopy{}, response.views.front().transformations}
					 : IntrusiveForwardList<Transformation>{});
}

void
SuffixRegistryLookup::OnTranslateError(std::exception_ptr ep) noexcept
{
	auto &_handler = handler;
	Destroy();

	_handler.OnSuffixRegistryError(ep);
}

void
suffix_registry_lookup(AllocatorPtr alloc,
		       TranslationService &service,
		       std::span<const std::byte> payload,
		       const char *suffix,
		       const StopwatchPtr &parent_stopwatch,
		       SuffixRegistryHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept
{
	auto lookup = alloc.New<SuffixRegistryLookup>(payload, suffix,
						      handler);

	service.SendRequest(alloc, lookup->request, parent_stopwatch,
			    *lookup, cancel_ptr);
}
