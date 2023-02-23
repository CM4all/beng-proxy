// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

/**
 * Wrapper for statx() which takes a directory path instead of a file
 * descriptor.
 */
bool
StatAt(const char *directory, const char *pathname, int flags,
       unsigned mask, struct statx *statxbuf) noexcept;
