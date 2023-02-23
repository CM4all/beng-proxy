// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Escaping with a static destination buffer.
 */

#pragma once

#include <string_view>

struct escape_class;

/**
 * Unescape the given string into a global static buffer.  Returns
 * NULL when the string is too long for the buffer.
 */
[[gnu::pure]]
const char *
unescape_static(const struct escape_class *cls, std::string_view p) noexcept;

[[gnu::pure]]
const char *
escape_static(const struct escape_class *cls, std::string_view p) noexcept;
