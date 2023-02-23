// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Various utilities for working with HTTP objects.
 */

#pragma once

class AllocatorPtr;

/**
 * Splits a comma separated list into a string array.  The return
 * value is nullptr terminated.
 */
const char *const*
http_list_split(AllocatorPtr alloc, const char *p) noexcept;
