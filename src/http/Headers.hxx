/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include "strmap.hxx"
#include "GrowingBuffer.hxx"
#include "HeaderWriter.hxx"
#include "HeaderParser.hxx"
#include "util/Compiler.h"

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
	 * Shall the HTTP server library generate a "Date" response
	 * header?
	 *
	 * @see RFC 2616 14.18
	 */
	bool generate_date_header = false;

	/**
	 * Shall the HTTP server library generate a "Server" response
	 * header?
	 *
	 * @see RFC 2616 3.8
	 */
	bool generate_server_header = false;

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

	StringMap &&ToMap(struct pool &pool) && noexcept {
		header_parse_buffer(pool, map, std::move(buffer));
		return std::move(map);
	}

	gcc_pure
	const char *Get(const char *key) const noexcept {
		return map.Get(key);
	}

	GrowingBuffer &GetBuffer() noexcept {
		return buffer;
	}

	GrowingBuffer MakeBuffer() noexcept {
		return std::move(buffer);
	}

	void Write(const char *name, const char *value) noexcept {
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
