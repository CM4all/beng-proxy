// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

enum class LookupCertResult {
	NOT_FOUND,
	COMPLETE,
	IN_PROGRESS,

	/**
	 * An error has occurred and the TLS handshake shall be
	 * aborted.
	 */
	ERROR,
};
