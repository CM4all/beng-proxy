// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * HTTP string utilities according to RFC 2616 2.2.
 */

#pragma once

#include <string_view>
#include <utility>

class AllocatorPtr;

std::string_view
http_next_quoted_string(AllocatorPtr alloc, std::string_view &input) noexcept;

std::string_view
http_next_value(AllocatorPtr alloc, std::string_view &input) noexcept;

std::pair<std::string_view, std::string_view>
http_next_name_value(AllocatorPtr alloc, std::string_view &input) noexcept;
