// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/StringBuffer.hxx"

[[gnu::pure]]
bool
IsAprMd5(const char *crypted_password) noexcept;

/**
 * Emulate APR's braindead apr_md5_encode() function.
 */
[[gnu::pure]]
StringBuffer<120>
AprMd5(const char *pw, const char *salt) noexcept;
