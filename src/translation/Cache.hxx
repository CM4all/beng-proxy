// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Service.hxx"

#include <memory>
#include <span>

enum class TranslationCommand : uint16_t;
class EventLoop;
struct CacheStats;

struct tcache;

/**
 * Cache for translation server responses.
 */
class TranslationCache final : public TranslationService {
	std::unique_ptr<struct tcache> cache;

public:
	/**
	 * @param handshake_cacheable if false, then all requests are
	 * deemed uncacheable until the first response is received
	 */
	TranslationCache(struct pool &pool, EventLoop &event_loop,
			 TranslationService &next,
			 unsigned max_size, bool handshake_cacheable=true);

	~TranslationCache() noexcept override;

	void ForkCow(bool inherit) noexcept;

	[[gnu::pure]]
	CacheStats GetStats() const noexcept;

	/**
	 * Flush all items from the cache.
	 */
	void Flush() noexcept;

	/**
	 * Flush selected items from the cache.
	 *
	 * @param request a request with parameters to compare with
	 * @param vary a list of #beng_translation_command codes which define
	 * the cache item filter
	 */
	void Invalidate(const TranslateRequest &request,
			std::span<const TranslationCommand> vary,
			const char *site) noexcept;

	/* virtual methods from class TranslationService */
	void SendRequest(AllocatorPtr alloc,
			 const TranslateRequest &request,
			 const StopwatchPtr &parent_stopwatch,
			 TranslateHandler &handler,
			 CancellablePointer &cancel_ptr) noexcept override;
};
