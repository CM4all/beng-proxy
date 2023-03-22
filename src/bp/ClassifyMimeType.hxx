// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <string_view>

/**
 * Is this a MIME type that contains text?
 */
[[gnu::pure]]
bool
IsTextMimeType(std::string_view type) noexcept;
