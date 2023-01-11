/*
 * Copyright 2007-2022 CM4all GmbH
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

class LbCluster;
class LbBranch;
class LbLuaHandler;
class LbTranslationHandler;
class LbPrometheusExporter;
struct LbSimpleHttpResponse;

struct LbGoto {
	LbCluster *cluster = nullptr;
	LbBranch *branch = nullptr;
	LbLuaHandler *lua = nullptr;
	LbTranslationHandler *translation = nullptr;
	LbPrometheusExporter *exporter = nullptr;
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
	LbGoto(LbPrometheusExporter &_exporter) noexcept:exporter(&_exporter) {}
	LbGoto(const LbSimpleHttpResponse &_response) noexcept:response(&_response) {}

	bool IsDefined() const noexcept {
		return cluster != nullptr || branch != nullptr ||
			lua != nullptr || translation != nullptr ||
			exporter != nullptr ||
			response != nullptr || resolve_connect != nullptr;
	}

	template<typename R>
	[[gnu::pure]]
	const LbGoto &FindRequestLeaf(const R &request) const noexcept;
};
