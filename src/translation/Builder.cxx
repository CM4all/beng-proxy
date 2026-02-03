// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Builder.hxx"
#include "Glue.hxx"
#include "Cache.hxx"
#include "stats/CacheStats.hxx"
#include "net/SocketAddress.hxx"

#include <algorithm> // for std::lexicographical_compare()
#include <cassert>
#include <cstring>

class SocketAddress;
class TranslationStock;

[[gnu::pure]]
inline bool
SocketAddressCompare::operator()(SocketAddress a,
				 SocketAddress b) const noexcept
{
	assert(!a.IsNull());
	assert(!b.IsNull());

	std::span<const std::byte> as{a};
	std::span<const std::byte> bs{b};

	return std::lexicographical_compare(as.begin(), as.end(), bs.begin(), bs.end());
}

TranslationStockBuilder::TranslationStockBuilder(unsigned _limit) noexcept
	:limit(_limit)
{
}

TranslationStockBuilder::~TranslationStockBuilder() noexcept = default;

std::shared_ptr<TranslationService>
TranslationStockBuilder::Get(SocketAddress address,
			     EventLoop &event_loop) noexcept
{
	auto e = m.try_emplace(address, nullptr);
	if (e.second)
		e.first->second = std::make_shared<TranslationGlue>
			(event_loop, address, limit);

	return e.first->second;
}

TranslationCacheBuilder::TranslationCacheBuilder(TranslationStockBuilder &_builder,
						 struct pool &_pool,
						 unsigned _max_size) noexcept
	:builder(_builder),
	 pool(_pool), max_size(_max_size)
{
}

TranslationCacheBuilder::~TranslationCacheBuilder() noexcept = default;

void
TranslationCacheBuilder::ForkCow(bool inherit) noexcept
{
	for (auto &i : m)
		i.second->ForkCow(inherit);
}

void
TranslationCacheBuilder::Populate() noexcept
{
	for (auto &i : m)
		i.second->Populate();
}

CacheStats
TranslationCacheBuilder::GetStats() const noexcept
{
	CacheStats stats{};

	for (const auto &i : m)
		stats += i.second->GetStats();

	return stats;
}

void
TranslationCacheBuilder::Flush() noexcept
{
	for (auto &i : m)
		i.second->Flush();
}

void
TranslationCacheBuilder::Invalidate(const TranslateRequest &request,
				    std::span<const TranslationCommand> vary,
				    const char *site, const char *tag) noexcept
{
	for (auto &i : m)
		i.second->Invalidate(request, vary, site, tag);
}

std::shared_ptr<TranslationService>
TranslationCacheBuilder::Get(SocketAddress address,
			     EventLoop &event_loop) noexcept
{
	auto e = m.try_emplace(address, nullptr);
	if (e.second)
		e.first->second = std::make_shared<TranslationCache>
			(pool, event_loop,
			 // TODO: refactor to std::shared_ptr?
			 *builder.Get(address, event_loop),
			 max_size, false);

	return e.first->second;
}
