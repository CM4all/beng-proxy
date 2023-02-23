// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Functions for working with relative URIs.
 */

#pragma once

#include <string_view>

/**
 * Check if an (absolute) URI is relative to an a base URI (also
 * absolute), and return the relative part.  Returns NULL if both URIs
 * do not match.
 */
[[gnu::pure]]
std::string_view
uri_relative(std::string_view base, std::string_view uri) noexcept;
