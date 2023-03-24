// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Handle the request/response headers for static files.
 */

#pragma once

#include <sys/stat.h>
#include <stddef.h>

struct pool;
class FileDescriptor;
class StringMap;
struct statx;

void
GetAnyETag(char *buffer, size_t size,
	   FileDescriptor fd, const struct statx &st) noexcept;

bool
load_xattr_content_type(char *buffer, size_t size, FileDescriptor fd) noexcept;

/**
 * @param fd a file descriptor for loading xattr, or -1 to disable
 * xattr
 */
StringMap
static_response_headers(struct pool &pool,
			FileDescriptor fd, const struct statx &st,
			const char *content_type) noexcept;
