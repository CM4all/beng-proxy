/*
 * Copyright 2007-2019 Content Management AG
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

#include <array>
#include <chrono>

class SessionId;

struct CsrfHash {
	std::array<uint8_t, 12> data;

	void Generate(std::chrono::system_clock::time_point time,
		      const SessionId &salt) noexcept;

	/**
	 * @return the first unparsed character or nullptr on error
	 */
	const char *Parse(const char *s) noexcept;

	bool operator==(const CsrfHash &other) const noexcept {
		return std::equal(data.begin(), data.end(), other.data.begin());
	}

	static constexpr uint32_t ImportTime(std::chrono::system_clock::time_point t) noexcept {
		return std::chrono::duration_cast<std::chrono::minutes>(t.time_since_epoch()).count();
	}

	static constexpr std::chrono::system_clock::time_point ExportTime(uint32_t t) noexcept {
		return std::chrono::system_clock::time_point(std::chrono::minutes(t));
	}
};

struct CsrfToken {
	std::chrono::system_clock::time_point time;
	CsrfHash hash;

	static constexpr size_t STRING_LENGTH = 16;

	void Generate(std::chrono::system_clock::time_point _time,
		      const SessionId &salt) noexcept {
		time = _time;
		hash.Generate(_time, salt);
	}

	/**
	 * @param s a buffer of at least STRING_LENGTH+1 bytes
	 */
	void Format(char *s) const noexcept;

	/**
	 * @return true on success
	 */
	bool Parse(const char *s) noexcept;
};
