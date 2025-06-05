// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "translation/Handler.hxx"
#include "pool/Ptr.hxx"

struct RecordingTranslateHandler final : TranslateHandler {
	PoolPtr pool;

	UniquePoolPtr<TranslateResponse> response;

	std::exception_ptr error;

	bool finished = false;

	explicit RecordingTranslateHandler(struct pool &parent_pool) noexcept;

	/* virtual methods from TranslateHandler */
	void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept override;
	void OnTranslateError(std::exception_ptr error) noexcept override;
};
