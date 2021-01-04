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

#include "suffix_registry.hxx"
#include "translation/Service.hxx"
#include "translation/Handler.hxx"
#include "translation/Request.hxx"
#include "translation/Response.hxx"
#include "widget/View.hxx"
#include "pool/pool.hxx"

struct SuffixRegistryLookup final : TranslateHandler {
	TranslateRequest request;

	SuffixRegistryHandler &handler;

	SuffixRegistryLookup(ConstBuffer<void> payload,
			     const char *suffix,
			     SuffixRegistryHandler &_handler) noexcept
		:handler(_handler) {
		request.content_type_lookup = payload;
		request.suffix = suffix;
	}

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(TranslateResponse &response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;
};

void
SuffixRegistryLookup::OnTranslateResponse(TranslateResponse &response) noexcept
{
	handler.OnSuffixRegistrySuccess(response.content_type,
					response.views != nullptr
					? IntrusiveForwardList<Transformation>{ShallowCopy{}, response.views->transformations}
					: IntrusiveForwardList<Transformation>{});
}

void
SuffixRegistryLookup::OnTranslateError(std::exception_ptr ep) noexcept
{
	handler.OnSuffixRegistryError(ep);
}

void
suffix_registry_lookup(struct pool &pool,
		       TranslationService &service,
		       ConstBuffer<void> payload,
		       const char *suffix,
		       const StopwatchPtr &parent_stopwatch,
		       SuffixRegistryHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept
{
	auto lookup = NewFromPool<SuffixRegistryLookup>(pool,
							payload, suffix,
							handler);

	service.SendRequest(pool, lookup->request, parent_stopwatch,
			    *lookup, cancel_ptr);
}
