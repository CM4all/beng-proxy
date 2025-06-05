// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "util/PackedBigEndian.hxx"

#include <cstdint>
#include <cstddef>

static constexpr uint8_t FCGI_VERSION_1 = 1;

enum class FcgiRecordType : uint8_t {
	BEGIN_REQUEST = 1,
	ABORT_REQUEST = 2,
	END_REQUEST = 3,
	PARAMS = 4,
	STDIN = 5,
	STDOUT = 6,
	STDERR = 7,
	DATA = 8,
	GET_VALUES = 9,
	GET_VALUES_RESULT = 10,
	UNKNOWN_TYPE = 11,
};

struct FcgiRecordHeader {
	uint8_t version;
	FcgiRecordType type;
	PackedBE16 request_id;
	PackedBE16 content_length;
	uint8_t padding_length;
	std::byte reserved;
	/*
	  std::byte content_data[content_length];
	  std::byte padding_data[padding_length];
	*/
};

static_assert(sizeof(FcgiRecordHeader) == 8, "Wrong FastCGI header size");
static_assert(alignof(FcgiRecordHeader) == 1);

/*
 * Values for role component of FCGI_BeginRequestBody
 */
enum class FcgiRole : uint16_t {
	RESPONDER = 1,
	AUTHORIZER = 2,
	FILTER = 3,
};

/*
 * Mask for flags component of FCGI_BeginRequestBody
 */
static constexpr uint8_t FCGI_FLAG_KEEP_CONN = 1;

struct FcgiBeginRequest {
	PackedBE16 role;
	uint8_t flags;
	uint8_t reserved[5];
};

static_assert(sizeof(FcgiBeginRequest) == 8, "Wrong FastCGI packet size");
static_assert(alignof(FcgiBeginRequest) == 1);

enum class FcgiProtocolStatus : uint8_t {
	REQUEST_COMPLETE = 0,
	CANT_MPX_CONN = 1,
	OVERLOADED = 2,
	UNKNOWN_ROLE = 3,
};

struct FcgiEndRequest {
	PackedBE32 app_status;
	uint8_t protocol_status;
	uint8_t reserved[3];
};

static_assert(sizeof(FcgiEndRequest) == 8);
static_assert(alignof(FcgiEndRequest) == 1);
