// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <span>

class SslFilter;

inline bool
IsAlpnHttp2(std::span<const unsigned char> alpn) noexcept
{
	return alpn.size() == 2 &&
		alpn[0] == 'h' && alpn[1] == '2';
}

[[gnu::pure]]
bool
IsAlpnHttp2(const SslFilter &ssl_filter) noexcept;

inline bool
IsAlpnHttp2(const SslFilter *ssl_filter) noexcept
{
	return ssl_filter != nullptr && IsAlpnHttp2(*ssl_filter);
}
