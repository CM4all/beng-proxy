// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

class LbCluster;
class LbBranch;
class LbLuaHandler;
class LbTranslationHandler;
class HttpServerRequestHandler;
struct LbSimpleHttpResponse;

struct LbGoto {
	LbCluster *cluster = nullptr;
	LbBranch *branch = nullptr;
	LbLuaHandler *lua = nullptr;
	LbTranslationHandler *translation = nullptr;
	HttpServerRequestHandler *handler = nullptr;
	const LbSimpleHttpResponse *response = nullptr;

	/**
	 * Resolve this host name and connect to the resulting
	 * address.
	 */
	const char *resolve_connect = nullptr;

	LbGoto() noexcept = default;
	LbGoto(LbCluster &_cluster) noexcept:cluster(&_cluster) {}
	LbGoto(LbBranch &_branch) noexcept:branch(&_branch) {}
	LbGoto(LbLuaHandler &_lua) noexcept:lua(&_lua) {}
	LbGoto(LbTranslationHandler &_translation) noexcept:translation(&_translation) {}
	LbGoto(HttpServerRequestHandler &_handler) noexcept:handler(&_handler) {}
	LbGoto(const LbSimpleHttpResponse &_response) noexcept:response(&_response) {}

	bool IsDefined() const noexcept {
		return cluster != nullptr || branch != nullptr ||
			lua != nullptr || translation != nullptr ||
			handler != nullptr ||
			response != nullptr || resolve_connect != nullptr;
	}

	template<typename R>
	[[gnu::pure]]
	const LbGoto &FindRequestLeaf(const R &request) const noexcept;
};
