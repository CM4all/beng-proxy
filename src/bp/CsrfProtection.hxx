// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "http/Method.hxx"

constexpr bool
MethodNeedsCsrfProtection(HttpMethod method) noexcept
{
	switch (method) {
	case HttpMethod::HEAD:
	case HttpMethod::GET:
	case HttpMethod::OPTIONS:
	case HttpMethod::TRACE:
	case HttpMethod::PROPFIND:
	case HttpMethod::REPORT:
		return false;

	default:
		return true;
	}
}

