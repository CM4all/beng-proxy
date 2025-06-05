// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "pool/UniquePtr.hxx"

#include <exception>

struct TranslateResponse;

class TranslateHandler {
public:
	virtual void OnTranslateResponse(UniquePoolPtr<TranslateResponse> response) noexcept = 0;
	virtual void OnTranslateError(std::exception_ptr error) noexcept = 0;
};
