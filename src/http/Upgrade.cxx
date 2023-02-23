// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Upgrade.hxx"
#include "Headers.hxx"
#include "strmap.hxx"

bool
http_is_upgrade(const StringMap &headers) noexcept
{
	return headers.Contains("upgrade");
}

bool
http_is_upgrade(const HttpHeaders &headers) noexcept
{
	return http_is_upgrade(headers.GetMap());
}
