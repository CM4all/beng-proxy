// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class AllocatorPtr;

[[gnu::pure]]
const char *
RelocateUri(AllocatorPtr alloc, const char *uri,
	    const char *internal_host, std::string_view internal_path,
	    const char *external_scheme, const char *external_host,
	    std::string_view external_path,
	    std::string_view base) noexcept;
