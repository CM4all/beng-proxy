// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/Method.hxx"

constexpr bool
MethodNeedsCsrfProtection(HttpMethod method) noexcept
{
	return !IsSafeMethod(method);
}
