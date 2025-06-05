// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Upgrade.hxx"
#include "CommonHeaders.hxx"
#include "Headers.hxx"
#include "strmap.hxx"

bool
http_is_upgrade(const StringMap &headers) noexcept
{
	return headers.Contains(upgrade_header);
}

bool
http_is_upgrade(const HttpHeaders &headers) noexcept
{
	return http_is_upgrade(headers.GetMap());
}
