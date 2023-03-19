// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "AllocatorPtr.hxx"
#include "strmap.hxx"
#include "memory/GrowingBuffer.hxx"
#include "util/SpanCast.hxx"
#include "HeaderWriter.hxx"
#include "HeaderParser.hxx"

/**
 * A class that stores HTTP headers in a map and a buffer.  Some
 * libraries want a map, some want a buffer, and this class attempts
 * to give each of them what they can cope with best.
 */
class HttpHeaders {
	StringMap map;

	GrowingBuffer buffer;

public:
	/**
	 * Does #buffer contain "Content-Encoding"?
	 */
	bool contains_content_encoding = false;

	/**
	 * Does #buffer contain "Content-Range"?
	 */
	bool contains_content_range = false;

	/**
	 * Shall the HTTP server library generate a "Date" response
	 * header?
	 *
	 * @see RFC 2616 14.18
	 */
	bool generate_date_header = true;

	/**
	 * Shall the HTTP server library generate a "Server" response
	 * header?
	 *
	 * @see RFC 2616 3.8
	 */
	bool generate_server_header = true;

	HttpHeaders() = default;

	explicit HttpHeaders(StringMap &&_map) noexcept
		:map(std::move(_map)) {}

	HttpHeaders(GrowingBuffer &&_buffer) noexcept
		:buffer(std::move(_buffer)) {}

	HttpHeaders(HttpHeaders &&) = default;
	HttpHeaders &operator=(HttpHeaders &&) = default;

	const StringMap &GetMap() const noexcept {
		return map;
	}

	StringMap &&ToMap(AllocatorPtr alloc) && noexcept {
		header_parse_buffer(alloc, map, std::move(buffer));
		return std::move(map);
	}

	/**
	 * Does the #StringMap contain a header with the specified
	 * name?
	 */
	[[gnu::pure]]
	bool MapContains(const char *key) const noexcept {
		return map.Contains(key);
	}

	bool ContainsContentEncoding() const noexcept {
		return contains_content_encoding || MapContains("content-encoding");
	}

	bool ContainsContentRange() const noexcept {
		return contains_content_range || MapContains("content-range");
	}

	/**
	 * Attempt to look up a header; if it is not found in the
	 * #map, the first part of #buffer is parsed, which may not
	 * find the header if it happens to be (partly) in a secondary
	 * buffer.
	 */
	[[gnu::pure]]
	std::string_view GetSloppy(const char *key) const noexcept {
		if (const char *value = map.Get(key); value != nullptr)
			return value;

		return header_parse_find(ToStringView(buffer.Read()), key);
	}

	GrowingBuffer &GetBuffer() noexcept {
		return buffer;
	}

	GrowingBuffer MakeBuffer() noexcept {
		return std::move(buffer);
	}

	void Write(std::string_view name, std::string_view value) noexcept {
		header_write(buffer, name, value);
	}

	/**
	 * Copy a (hop-by-hop) header from a map to the buffer.
	 */
	void CopyToBuffer(const StringMap &src, const char *name) noexcept {
		const char *value = src.Get(name);
		if (value != nullptr)
			Write(name, value);
	}

	/**
	 * Move a (hop-by-hop) header from the map to the buffer.
	 */
	void MoveToBuffer(const char *name) noexcept {
		CopyToBuffer(map, name);
	}

	void MoveToBuffer(const char *const*names) noexcept {
		for (; *names != nullptr; ++names)
			MoveToBuffer(*names);
	}

	GrowingBuffer &&ToBuffer() noexcept {
		headers_copy_most(map, buffer);
		return std::move(buffer);
	}
};
