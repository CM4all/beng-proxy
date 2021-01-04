/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "Builder.hxx"
#include "Stock.hxx"
#include "Cache.hxx"
#include "net/SocketAddress.hxx"
#include "util/ConstBuffer.hxx"
#include "AllocatorStats.hxx"

#include <cassert>
#include <cstring>

class SocketAddress;
class TranslationStock;

gcc_pure
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
	auto e = m.emplace(address, nullptr);
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

AllocatorStats
TranslationCacheBuilder::GetStats() const noexcept
{
	AllocatorStats stats = AllocatorStats::Zero();

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
				    ConstBuffer<TranslationCommand> vary,
				    const char *site) noexcept
{
	for (auto &i : m)
		i.second->Invalidate(request, vary, site);
}

std::shared_ptr<TranslationService>
TranslationCacheBuilder::Get(SocketAddress address,
			     EventLoop &event_loop) noexcept
{
	auto e = m.emplace(address, nullptr);
	if (e.second)
		e.first->second = std::make_shared<TranslationCache>
			(pool, event_loop,
			 // TODO: refactor to std::shared_ptr?
			 *builder.Get(address, event_loop),
			 max_size, false);

	return e.first->second;
}
