// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

enum class FailureStatus {
	/**
	 * No failure, host is ok.
	 */
	OK,

	/**
	 * Host is being faded out (graceful shutdown).  No new sessions.
	 */
	FADE,

	/**
	 * A server-side protocol-level failure.
	 */
	PROTOCOL,

	/**
	 * Failed to connect to the host.
	 */
	CONNECT,

	/**
	 * The failure was submitted by a "monitor", and will not expire
	 * until the monitor detects recovery.
	 */
	MONITOR,
};
