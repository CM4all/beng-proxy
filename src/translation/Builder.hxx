// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/BindMethod.hxx"

#include <cstdint>
#include <map>
#include <memory>
#include <span>
#include <string_view>

struct CacheStats;
class EventLoop;
class SocketAddress;
class TranslationGlue;
class TranslationCache;
class TranslationService;
struct TranslateRequest;
struct TranslateResponse;
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

	using ExpireCallback = BoundMethod<void(const TranslateResponse &response) noexcept>;

public:
	TranslationCacheBuilder(TranslationStockBuilder &_builder,
				struct pool &_pool,
				unsigned _max_size) noexcept;
	~TranslationCacheBuilder() noexcept;

	void ForkCow(bool inherit) noexcept;
	void Populate() noexcept;

	[[gnu::pure]]
	CacheStats GetStats() const noexcept;

	void Flush() noexcept;

	void Invalidate(const TranslateRequest &request,
			std::span<const TranslationCommand> vary,
			const char *site, const char *tag) noexcept;

	void ExpireTag(std::string_view tag, ExpireCallback callback) noexcept;

	std::shared_ptr<TranslationService> Get(SocketAddress address,
						EventLoop &event_loop) noexcept override;
};
