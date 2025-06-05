// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "event/Chrono.hxx"

#include <string>

struct LbMonitorConfig {
	std::string name;

	/**
	 * Time in seconds between two monitor checks.
	 */
	Event::Duration interval = std::chrono::seconds(10);

	/**
	 * If the monitor does not produce a result after this timeout
	 * [seconds], it is assumed to be negative.
	 */
	Event::Duration timeout{};

	enum class Type {
		NONE,
		PING,
		CONNECT,
		TCP_EXPECT,
	} type = Type::NONE;

	/**
	 * The timeout for establishing a connection.  Only applicable for
	 * #Type::TCP_EXPECT.  0 means no special setting present.
	 */
	Event::Duration connect_timeout{};

	/**
	 * For #Type::TCP_EXPECT: a string that is sent to the peer
	 * after the connection has been established.  May be empty.
	 */
	std::string send;

	/**
	 * For #Type::TCP_EXPECT: a string that is expected to be
	 * received from the peer after the #send string has been sent.
	 */
	std::string expect;

	/**
	 * For #Type::TCP_EXPECT: if that string is received from the
	 * peer (instead of #expect), then the node is assumed to be
	 * shutting down gracefully, and will only get sticky requests.
	 */
	std::string fade_expect;

	explicit LbMonitorConfig(const char *_name) noexcept
		:name(_name) {}

	bool IsDefined() const noexcept {
		return type != Type::NONE;
	}
};
