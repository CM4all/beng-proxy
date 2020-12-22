/*
 * Copyright 2007-2020 CM4all GmbH
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

#pragma once

#include <cstdint>
#include <map>

template<typename T> struct ConstBuffer;
struct AllocatorStats;
class EventLoop;
class SocketAddress;
class TranslationStock;
class TranslationCache;
class TranslationService;
struct TranslateRequest;
enum class TranslationCommand : uint16_t;

struct SocketAddressCompare {
	bool operator()(SocketAddress a, SocketAddress b) const noexcept;
};

class TranslationStockBuilder {
	const unsigned limit;

	std::map<SocketAddress, TranslationStock,
		 SocketAddressCompare> m;

public:
	explicit TranslationStockBuilder(unsigned _limit) noexcept;
	~TranslationStockBuilder() noexcept;

	TranslationService &Get(SocketAddress address,
				EventLoop &event_loop) noexcept;
};

class TranslationCacheBuilder {
	TranslationStockBuilder &builder;

	struct pool &pool;

	const unsigned max_size;

	std::map<SocketAddress, TranslationCache,
		 SocketAddressCompare> m;

public:
	TranslationCacheBuilder(TranslationStockBuilder &_builder,
				struct pool &_pool,
				unsigned _max_size) noexcept;
	~TranslationCacheBuilder() noexcept;

	void ForkCow(bool inherit) noexcept;

	AllocatorStats GetStats() const noexcept;

	void Flush() noexcept;

	void Invalidate(const TranslateRequest &request,
			ConstBuffer<TranslationCommand> vary,
			const char *site) noexcept;

	TranslationService &Get(SocketAddress address,
				EventLoop &event_loop) noexcept;
};