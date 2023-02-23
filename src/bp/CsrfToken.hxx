// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <array>
#include <chrono>

class SessionId;

struct CsrfHash {
	std::array<std::byte, 12> data;

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

	static constexpr size_t STRING_LENGTH = 32;

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
