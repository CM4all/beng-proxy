// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/IntrusiveForwardList.hxx"

#include <cstddef>
#include <exception>
#include <span>

class AllocatorPtr;
class TranslationService;
class StopwatchPtr;
class CancellablePointer;
struct Transformation;

class SuffixRegistryHandler {
public:
	/**
	 * @param transformations an optional #Transformation chain for
	 * all files of this type
	 */
	virtual void OnSuffixRegistrySuccess(const char *content_type,
					     bool auto_gzipped, bool auto_brotli_path, bool auto_brotli,
					     const IntrusiveForwardList<Transformation> &transformations) noexcept = 0;

	virtual void OnSuffixRegistryError(std::exception_ptr ep) noexcept = 0;
};

/**
 * Interface for Content-Types managed by the translation server.
 */
void
suffix_registry_lookup(AllocatorPtr alloc,
		       TranslationService &service,
		       std::span<const std::byte> payload,
		       const char *suffix,
		       const StopwatchPtr &parent_stopwatch,
		       SuffixRegistryHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept;
