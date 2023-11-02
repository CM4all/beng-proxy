// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ConditionConfig.hxx"
#include "Protocol.hxx"
#include "SimpleHttpResponse.hxx"
#include "net/AllocatedSocketAddress.hxx"
#include "util/StringLess.hxx"
#include "config.h"

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
struct LbPrometheusDiscoveryConfig;

struct LbGotoConfig {
	std::variant<std::monostate,
		     const LbClusterConfig *,
		     const LbBranchConfig *,
		     const LbLuaHandlerConfig *,
		     const LbTranslationHandlerConfig *,
		     const LbPrometheusExporterConfig *,
#ifdef HAVE_AVAHI
		     const LbPrometheusDiscoveryConfig *,
#endif
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

#ifdef HAVE_AVAHI
	explicit LbGotoConfig(const LbPrometheusDiscoveryConfig &_discovery) noexcept
		:destination(&_discovery) {}
#endif // HAVE_AVAHI

	explicit LbGotoConfig(HttpStatus _status) noexcept
		:destination(LbSimpleHttpResponse{_status}) {}

	bool IsDefined() const noexcept {
		return !std::holds_alternative<std::monostate>(destination);
	}

	[[gnu::pure]]
	LbProtocol GetProtocol() const noexcept;

	[[gnu::pure]]
	const char *GetName() const noexcept;

#ifdef HAVE_AVAHI
	[[gnu::pure]]
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
