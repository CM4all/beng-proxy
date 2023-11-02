// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "config.h"

#include <variant>

class LbCluster;
class LbBranch;
class LbLuaHandler;
class LbTranslationHandler;
class HttpServerRequestHandler;
struct LbSimpleHttpResponse;

/**
 * Resolve this host name and connect to the resulting
 * address.
 */
struct LbResolveConnect {
	const char *host;
};

struct LbGoto {
	std::variant<std::monostate,
		     LbCluster *,
		     LbBranch *,
#ifdef HAVE_LUA
		     LbLuaHandler *,
#endif
		     LbTranslationHandler *,
		     HttpServerRequestHandler *,
		     const LbSimpleHttpResponse *,
		     LbResolveConnect> destination;

	constexpr LbGoto() noexcept = default;

	constexpr LbGoto(LbCluster &cluster) noexcept
		:destination(&cluster) {}

	constexpr LbGoto(LbBranch &branch) noexcept
		:destination(&branch) {}

#ifdef HAVE_LUA
	constexpr LbGoto(LbLuaHandler &lua) noexcept
		:destination(&lua) {}
#endif

	constexpr LbGoto(LbTranslationHandler &translation) noexcept
		:destination(&translation) {}

	constexpr LbGoto(HttpServerRequestHandler &handler) noexcept
		:destination(&handler) {}

	constexpr LbGoto(const LbSimpleHttpResponse &response) noexcept
		:destination(&response) {}

	constexpr LbGoto(const LbResolveConnect &resolve_connect) noexcept
		:destination(resolve_connect) {}

	bool IsDefined() const noexcept {
		return destination.index() != 0;
	}

	template<typename R>
	[[gnu::pure]]
	const LbGoto &FindRequestLeaf(const R &request) const noexcept;
};
