// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Write HTTP headers into a buffer.
 */

#pragma once

#include <chrono>
#include <string_view>

class StringMap;
class GrowingBuffer;

/**
 * Begin writing a header line.  After this, you may write the value.
 * Call header_write_finish() when you're done.
 */
void
header_write_begin(GrowingBuffer &buffer, std::string_view name) noexcept;

/**
 * Finish the current header line.
 *
 * @see header_write_begin().
 */
void
header_write_finish(GrowingBuffer &buffer) noexcept;

void
header_write(GrowingBuffer &buffer,
	     std::string_view name, std::string_view value) noexcept;

void
header_write(GrowingBuffer &headers, std::string_view name,
	     std::chrono::system_clock::time_point value) noexcept;

void
headers_copy_one(const StringMap &in, GrowingBuffer &out,
		 const char *key) noexcept;

void
headers_copy(const StringMap &in, GrowingBuffer &out,
	     const char *const*keys) noexcept;

void
headers_copy_all(const StringMap &in, GrowingBuffer &out) noexcept;

/**
 * Like headers_copy_all(), but doesn't copy hop-by-hop headers.
 */
void
headers_copy_most(const StringMap &in, GrowingBuffer &out) noexcept;

GrowingBuffer
headers_dup(const StringMap &in) noexcept;
