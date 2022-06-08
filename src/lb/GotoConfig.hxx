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

#include "ConditionConfig.hxx"
#include "Protocol.hxx"
#include "SimpleHttpResponse.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/StringLess.hxx"

#include <filesystem>
#include <string>
#include <list>
#include <map>
#include <variant>

struct LbClusterConfig;
struct LbBranchConfig;
struct LbLuaHandlerConfig;
struct LbTranslationHandlerConfig;
struct LbPrometheusExporterConfig;

struct LbGotoConfig {
	std::variant<std::monostate,
		     const LbClusterConfig *,
		     const LbBranchConfig *,
		     const LbLuaHandlerConfig *,
		     const LbTranslationHandlerConfig *,
		     const LbPrometheusExporterConfig *,
		     LbSimpleHttpResponse> destination;

	LbGotoConfig() = default;

	explicit LbGotoConfig(const LbClusterConfig &_cluster) noexcept
		:destination(&_cluster) {}

	explicit LbGotoConfig(const LbBranchConfig &_branch) noexcept
		:destination(&_branch) {}

	explicit LbGotoConfig(const LbLuaHandlerConfig &_lua) noexcept
		:destination(&_lua) {}

	explicit LbGotoConfig(const LbTranslationHandlerConfig &_translation) noexcept
		:destination(&_translation) {}

	explicit LbGotoConfig(const LbPrometheusExporterConfig &_exporter) noexcept
		:destination(&_exporter) {}

	explicit LbGotoConfig(http_status_t _status) noexcept
		:destination(LbSimpleHttpResponse{_status}) {}

	bool IsDefined() const noexcept {
		return !std::holds_alternative<std::monostate>(destination);
	}

	[[gnu::pure]]
	LbProtocol GetProtocol() const noexcept;

	[[gnu::pure]]
	const char *GetName() const noexcept;

#ifdef HAVE_AVAHI
	bool HasZeroConf() const noexcept;
#endif
};

struct LbGotoIfConfig {
	LbConditionConfig condition;

	LbGotoConfig destination;

	LbGotoIfConfig(LbConditionConfig &&c, LbGotoConfig d) noexcept
		:condition(std::move(c)), destination(d) {}

#ifdef HAVE_AVAHI
	bool HasZeroConf() const {
		return destination.HasZeroConf();
	}
#endif
};

/**
 * An object that distributes connections or requests to the "real"
 * cluster.
 */
struct LbBranchConfig {
	std::string name;

	LbGotoConfig fallback;

	std::list<LbGotoIfConfig> conditions;

	explicit LbBranchConfig(const char *_name) noexcept
		:name(_name) {}

	LbBranchConfig(LbBranchConfig &&) = default;

	LbBranchConfig(const LbBranchConfig &) = delete;
	LbBranchConfig &operator=(const LbBranchConfig &) = delete;

	bool HasFallback() const noexcept {
		return fallback.IsDefined();
	}

	LbProtocol GetProtocol() const noexcept {
		return fallback.GetProtocol();
	}

#ifdef HAVE_AVAHI
	bool HasZeroConf() const noexcept;
#endif
};

/**
 * An HTTP request handler implemented in Lua.
 */
struct LbLuaHandlerConfig {
	std::string name;

	std::filesystem::path path;
	std::string function;

	explicit LbLuaHandlerConfig(const char *_name) noexcept
		:name(_name) {}

	LbLuaHandlerConfig(LbLuaHandlerConfig &&) = default;

	LbLuaHandlerConfig(const LbLuaHandlerConfig &) = delete;
	LbLuaHandlerConfig &operator=(const LbLuaHandlerConfig &) = delete;
};

struct LbTranslationHandlerConfig {
	std::string name;

	AllocatedSocketAddress address;

	std::map<const char *, LbGotoConfig, StringLess> destinations;

	explicit LbTranslationHandlerConfig(const char *_name) noexcept
		:name(_name) {}
};
