// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Builder.hxx"
#include "Stock.hxx"
#include "Cache.hxx"
#include "stats/CacheStats.hxx"
#include "net/SocketAddress.hxx"

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

	auto ad = (const std::byte *)a.GetAddress();
	auto bd = (const std::byte *)b.GetAddress();

	const auto size = std::min(a.GetSize(), b.GetSize());
	int cmp = memcmp(ad, bd, size);
	if (cmp != 0)
		return cmp < 0;

	return a.GetSize() < b.GetSize();
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
		e.first->second = std::make_shared<TranslationStock>
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
				    const char *site) noexcept
{
	for (auto &i : m)
		i.second->Invalidate(request, vary, site);
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
