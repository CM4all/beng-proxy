// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <string_view>

class AllocatorPtr;
struct escape_class;

std::string_view
escape_dup(AllocatorPtr alloc, const struct escape_class &cls,
	   std::string_view p) noexcept;

std::string_view
unescape_dup(AllocatorPtr alloc, const struct escape_class &cls,
	     std::string_view src) noexcept;
