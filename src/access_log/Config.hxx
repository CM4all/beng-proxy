// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "ChildErrorLogOptions.hxx"
#include "http/XForwardedFor.hxx"
#include "net/AllocatedSocketAddress.hxx"

#include <map>
#include <string>

/**
 * Configuration which describes whether and how to log HTTP requests.
 */
struct AccessLogConfig {
	enum class Type {
		DISABLED,
		INTERNAL,
		SEND,
		EXECUTE,
	} type = Type::INTERNAL;

	/**
	 * An address where we will send access log datagrams.
	 */
	AllocatedSocketAddress send_to;

	/**
	 * A command to be executed with a shell, where fd0 is a socket
	 * which receives access log datagrams.
	 *
	 * Special value "null" specifies that access logging is disabled
	 * completely, and "" (empty string) specifies that one-line
	 * logging is performed directly to standard output.
	 */
	std::string command;

	/**
	 * Don't log this request URI if host=="localhost" and
	 * status==200.
	 */
	std::string ignore_localhost_200;

	XForwardedForConfig xff;

	ChildErrorLogOptions child_error_options;

	/**
	 * Forward error messages printed by child processes into their
	 * stderr pipe to the Pond server?
	 */
	bool forward_child_errors = false;
};

/**
 * Helper structure which holds the configuration of a "main" (or
 * "default") access logger and an arbitrary number of named access
 * loggers.
 */
struct MultiAccessLogConfig {
	AccessLogConfig main;

	std::map<std::string, AccessLogConfig, std::less<>> named;

	[[gnu::pure]]
	const AccessLogConfig *Find(std::string_view name) const noexcept {
		if (name.empty())
			return &main;

		if (auto i = named.find(name); i != named.end())
			return &i->second;

		return nullptr;
	}

	const XForwardedForConfig *FindXForwardedForConfig(std::string_view name) const noexcept {
		if (const auto *i = Find(name); i != nullptr && !i->xff.empty())
			return &i->xff;

		return nullptr;
	}
};
