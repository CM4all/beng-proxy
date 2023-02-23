// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "ChildErrorLogOptions.hxx"
#include "http/XForwardedFor.hxx"
#include "net/AllocatedSocketAddress.hxx"

#include <string>
#include <set>

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

	/**
	 * Setter for the deprecated "--access-logger" command-line
	 * option, which has a few special cases.
	 */
	void SetLegacy(const char *new_value) {
		command = new_value;

		if (command.empty() || command == "internal")
			type = Type::INTERNAL;
		else if (command == "null")
			type = Type::DISABLED;
		else
			type = Type::EXECUTE;
	}
};
