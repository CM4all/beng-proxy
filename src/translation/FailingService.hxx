// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Service.hxx"

/**
 * A #TranslationService implementation that always fails.  Used by
 * unit tests.
 */
class FailingTranslationService final : public TranslationService {
public:
	/* virtual methods from class TranslationService */
	virtual void SendRequest(AllocatorPtr alloc,
				 const TranslateRequest &request,
				 const StopwatchPtr &parent_stopwatch,
				 TranslateHandler &handler,
				 CancellablePointer &cancel_ptr) noexcept override;
};
