// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Request.hxx"
#include "translation/Protocol.hxx"
#include "util/StaticVector.hxx"

#include <cstddef>
#include <span>
#include <string>

class AllocatorPtr;

struct TranslationInvalidateRequest : TranslateRequest {
	const char *site = nullptr;

	StaticVector<TranslationCommand, 32> commands;

	std::string ToString() const noexcept;
};

/**
 * Throws on error.
 */
TranslationInvalidateRequest
ParseTranslationInvalidateRequest(AllocatorPtr alloc,
				  std::span<const std::byte> p);
