// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

struct pool;
class UnusedIstreamPtr;

/**
 * istream implementation which reads from a string.
 */
UnusedIstreamPtr
istream_string_new(struct pool &pool, std::string_view s) noexcept;
