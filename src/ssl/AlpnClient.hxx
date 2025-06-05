// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

enum class SslClientAlpn {
	NONE,
	HTTP_2,

	/**
	 * HTTP/2 or HTTP/1.1.
	 */
	HTTP_ANY,
};
