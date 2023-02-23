// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AlpnCompare.hxx"
#include "Filter.hxx"

bool
IsAlpnHttp2(const SslFilter &ssl_filter) noexcept
{
	return IsAlpnHttp2(ssl_filter_get_alpn_selected(ssl_filter));
}
