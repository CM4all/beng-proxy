// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Various utilities for working with HTTP objects.
 */

#pragma once

#include <string_view>

[[gnu::pure]]
std::string_view
http_header_param(const char *value, const char *name) noexcept;
