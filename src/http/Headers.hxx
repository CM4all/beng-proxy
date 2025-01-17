// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "CommonHeaders.hxx"
#include "HeaderParser.hxx"
#include "HeaderWriter.hxx"
#include "memory/GrowingBuffer.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"
#include "strmap.hxx"

/**
 * A class that stores HTTP headers in a map and a buffer.  Some
 * libraries want a map, some want a buffer, and this class attempts
 * to give each of them what they can cope with best.
 */
class HttpHeaders {
	StringMap map;

	GrowingBuffer buffer;

	/**
	 * Reserve this number of bytes at the beginning (for the
	 * status line which the HTTP/1.1 server will prepend here).
	 */
	static constexpr std::size_t RESERVE = 64;

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

	HttpHeaders() noexcept {
		buffer.Reserve(RESERVE);
	}

	explicit HttpHeaders(StringMap &&_map) noexcept
		:map(std::move(_map))
	{
		buffer.Reserve(RESERVE);
	}

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
	bool MapContains(StringMapKey key) const noexcept {
		return map.Contains(key);
	}

	bool ContainsContentEncoding() const noexcept {
		return contains_content_encoding || MapContains(content_encoding_header);
	}

	bool ContainsContentRange() const noexcept {
		return contains_content_range || MapContains(content_range_header);
	}

	/**
	 * Attempt to look up a header; if it is not found in the
	 * #map, the first part of #buffer is parsed, which may not
	 * find the header if it happens to be (partly) in a secondary
	 * buffer.
	 */
	[[gnu::pure]]
	std::string_view GetSloppy(StringMapKey key) const noexcept {
		if (const char *value = map.Get(key); value != nullptr)
			return value;

		return header_parse_find(ToStringView(buffer.Read()), key.string);
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

	void VFmt(std::string_view name, fmt::string_view format_str, fmt::format_args args) noexcept {
		header_write_begin(buffer, name);
		buffer.VFmt(format_str, args);
		header_write_finish(buffer);
	}

	template<typename S, typename... Args>
	void Fmt(std::string_view name, const S &format_str, Args&&... args) noexcept {
		return VFmt(name, format_str, fmt::make_format_args(args...));
	}

	/**
	 * Copy a (hop-by-hop) header from a map to the buffer.
	 */
	void CopyToBuffer(const StringMap &src, StringMapKey name) noexcept {
		const char *value = src.Get(name);
		if (value != nullptr)
			Write(name.string, value);
	}

	/**
	 * Move a (hop-by-hop) header from the map to the buffer.
	 */
	void MoveToBuffer(StringMapKey name) noexcept {
		CopyToBuffer(map, name);
	}

	GrowingBuffer &&ToBuffer() noexcept {
		headers_copy_most(map, buffer);
		return std::move(buffer);
	}
};
