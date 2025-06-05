// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

struct ResourceAddress;
class AllocatorPtr;
class TranslationService;
class SuffixRegistryHandler;
class StopwatchPtr;
class CancellablePointer;

/**
 * Interface for Content-Types managed by the translation server.
 */
bool
suffix_registry_lookup(AllocatorPtr alloc, TranslationService &service,
		       const ResourceAddress &address,
		       const StopwatchPtr &parent_stopwatch,
		       SuffixRegistryHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept;
