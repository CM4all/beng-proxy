// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

/*
 * Serialize and deserialize FastCGI packets.
 */

#pragma once

#include <string_view>

#include <stdint.h>
#include <stddef.h>

enum class FcgiRecordType : uint8_t;
struct FcgiRecordHeader;
class GrowingBuffer;
class StringMap;

class FcgiRecordSerializer {
	GrowingBuffer &buffer;
	FcgiRecordHeader *const header;

public:
	FcgiRecordSerializer(GrowingBuffer &_buffer, FcgiRecordType type,
			     uint16_t request_id_be) noexcept;

	GrowingBuffer &GetBuffer() {
		return buffer;
	}

	void Commit(size_t content_length) noexcept;
};

class FcgiParamsSerializer {
	FcgiRecordSerializer record;

	size_t content_length = 0;

public:
	FcgiParamsSerializer(GrowingBuffer &_buffer,
			     uint16_t request_id_be) noexcept;

	FcgiParamsSerializer &operator()(std::string_view name,
					 std::string_view value) noexcept;

	FcgiParamsSerializer &operator()(std::string_view name,
					 const char *value) noexcept {
		return operator()(name,
				  value != nullptr
				  ? std::string_view{value}
				  : std::string_view{});
	}

	void Headers(const StringMap &headers) noexcept;

	void Commit() noexcept {
		record.Commit(content_length);
	}
};
