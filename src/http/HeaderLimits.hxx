// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/ByteSizes.hxx"

#include <cstddef>

/**
 * The maximum length of one HTTP header.
 */
static constexpr std::size_t MAX_HTTP_HEADER_SIZE = 8_KiB;

/**
 * The maximum total size of all HTTP headers.
 */
static constexpr std::size_t MAX_TOTAL_HTTP_HEADER_SIZE = 64_KiB;
