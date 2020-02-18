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

#include "util/StringBuffer.hxx"
#include "util/Compiler.h"

#include <array>
#include <algorithm>

#include <stddef.h>
#include <stdint.h>

/**
 * The session id data structure.
 */
class SessionId {
	std::array<uint64_t, 2> data;

public:
	gcc_pure
	bool IsDefined() const noexcept {
		return !std::all_of(data.begin(), data.end(), [](auto i){
			return i == 0;
		});
	}

	void Clear() noexcept {
		std::fill(data.begin(), data.end(), 0);
	}

	void Generate() noexcept;

	/**
	 * Manipulate the modulo of GetClusterHash() so that it results in
	 * the specified cluster node.
	 */
	void SetClusterNode(unsigned cluster_size, unsigned cluster_node) noexcept;

	gcc_pure
	bool operator==(const SessionId &other) const noexcept {
		return std::equal(data.begin(), data.end(), other.data.begin());
	}

	gcc_pure
	bool operator!=(const SessionId &other) const noexcept {
		return !(*this == other);
	}

	gcc_pure
	size_t Hash() const noexcept {
		return data[0];
	}

	/**
	 * Returns a hash that can be used to determine the cluster node
	 * by calculating the modulo.
	 */
	gcc_pure
	auto GetClusterHash() const noexcept {
		return data.back();
	}

	/**
	 * Parse a session id from a string.
	 *
	 * @return true on success, false on error
	 */
	bool Parse(const char *p) noexcept;

	gcc_pure
	StringBuffer<sizeof(data) * 2 + 1> Format() const noexcept;
};
