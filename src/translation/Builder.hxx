// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstdint>
#include <map>
#include <memory>
#include <span>

struct CacheStats;
class EventLoop;
class SocketAddress;
class TranslationGlue;
class TranslationCache;
class TranslationService;
struct TranslateRequest;
enum class TranslationCommand : uint16_t;

struct SocketAddressCompare {
	bool operator()(SocketAddress a, SocketAddress b) const noexcept;
};

class TranslationServiceBuilder {
public:
	virtual std::shared_ptr<TranslationService> Get(SocketAddress address,
							EventLoop &event_loop) noexcept = 0;
};

class TranslationStockBuilder final : public TranslationServiceBuilder {
	const unsigned limit;

	std::map<SocketAddress, std::shared_ptr<TranslationGlue>,
		 SocketAddressCompare> m;

public:
	explicit TranslationStockBuilder(unsigned _limit) noexcept;
	~TranslationStockBuilder() noexcept;

	std::shared_ptr<TranslationService> Get(SocketAddress address,
						EventLoop &event_loop) noexcept override;
};

class TranslationCacheBuilder final : public TranslationServiceBuilder {
	TranslationStockBuilder &builder;

	struct pool &pool;

	const unsigned max_size;

	std::map<SocketAddress, std::shared_ptr<TranslationCache>,
		 SocketAddressCompare> m;

public:
	TranslationCacheBuilder(TranslationStockBuilder &_builder,
				struct pool &_pool,
				unsigned _max_size) noexcept;
	~TranslationCacheBuilder() noexcept;

	void ForkCow(bool inherit) noexcept;

	[[gnu::pure]]
	CacheStats GetStats() const noexcept;

	void Flush() noexcept;

	void Invalidate(const TranslateRequest &request,
			std::span<const TranslationCommand> vary,
			const char *site) noexcept;

	std::shared_ptr<TranslationService> Get(SocketAddress address,
						EventLoop &event_loop) noexcept override;
};
