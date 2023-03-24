// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Handle the request/response headers for static files.
 */

#pragma once

#include "http/Range.hxx"

#include <chrono>

#include <sys/types.h>

template<typename Clock> class ClockCache;
class FileDescriptor;
class GrowingBuffer;
struct statx;

struct file_request {
	HttpRangeRequest range;

	explicit constexpr file_request(off_t _size) noexcept:range(_size) {}
};

void
file_response_headers(GrowingBuffer &headers,
		      const ClockCache<std::chrono::system_clock> &system_clock,
		      const char *override_content_type,
		      FileDescriptor fd, const struct statx &st,
		      std::chrono::seconds expires_relative,
		      bool processor_first, bool use_xattr) noexcept;
