// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include <cstddef>
#include <span>

class AllocatorPtr;

/**
 * Calculate the digest of the given buffer and return it as a HTTP
 * "Digest" header.
 *
 * @see https://developer.mozilla.org/en-US/docs/Web/HTTP/Headers/Digest
 */
[[gnu::pure]]
const char *
GenerateDigestHeader(AllocatorPtr alloc, std::span<const std::byte> src) noexcept;
