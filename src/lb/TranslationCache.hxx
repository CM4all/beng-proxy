// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "io/Logger.hxx"
#include "util/StaticCache.hxx"

#include <string>

enum class HttpStatus : uint_least16_t;
struct IncomingHttpRequest;
struct TranslationInvalidateRequest;
struct TranslateResponse;

class LbTranslationCache final {
	const LLogger logger;

public:
	struct Item {
		HttpStatus status = {};
		uint16_t https_only = 0;
		std::string redirect, message, pool, canonical_host, site;

		[[nodiscard]]
		explicit Item(const TranslateResponse &response) noexcept;

		[[gnu::pure]]
		size_t GetAllocatedMemory() const noexcept {
			return sizeof(*this) + redirect.length() + message.length() +
				pool.length() + canonical_host.length() + site.length();
		}
	};

	struct Vary {
		bool host = false;
		bool listener_tag = false;

	public:
		constexpr Vary() noexcept = default;

		[[nodiscard]]
		explicit Vary(const TranslateResponse &response) noexcept;

		[[nodiscard]]
		constexpr operator bool() const noexcept {
			return host || listener_tag;
		}

		void Clear() noexcept {
			host = false;
			listener_tag = false;
		}

		Vary &operator|=(const Vary other) noexcept {
			host |= other.host;
			listener_tag |= other.listener_tag;
			return *this;
		}
	};

private:
	using Cache = StaticCache<std::string, Item, 65536, 131071,
		std::hash<std::string_view>, std::equal_to<std::string_view>>;
	Cache cache;

	Vary seen_vary;

public:
	[[nodiscard]]
	LbTranslationCache() noexcept
		:logger("tcache") {}

	[[gnu::pure]]
	size_t GetAllocatedMemory() const noexcept;

	void Clear() noexcept;
	void Invalidate(const TranslationInvalidateRequest &request) noexcept;

	[[nodiscard]]
	const Item *Get(const IncomingHttpRequest &request,
			const char *listener_tag) noexcept;

	void Put(const IncomingHttpRequest &request,
		 const char *listener_tag,
		 const TranslateResponse &response) noexcept;
};
