// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <cstdint>
#include <cstddef>

#define FCGI_VERSION_1 1

#define FCGI_BEGIN_REQUEST       1
#define FCGI_ABORT_REQUEST       2
#define FCGI_END_REQUEST         3
#define FCGI_PARAMS              4
#define FCGI_STDIN               5
#define FCGI_STDOUT              6
#define FCGI_STDERR              7
#define FCGI_DATA                8
#define FCGI_GET_VALUES          9
#define FCGI_GET_VALUES_RESULT  10
#define FCGI_UNKNOWN_TYPE       11
#define FCGI_MAXTYPE (FCGI_UNKNOWN_TYPE)

/*
 * Mask for flags component of FCGI_BeginRequestBody
 */
#define FCGI_KEEP_CONN  1

/*
 * Values for role component of FCGI_BeginRequestBody
 */
#define FCGI_RESPONDER  1
#define FCGI_AUTHORIZER 2
#define FCGI_FILTER     3

struct fcgi_record_header {
	uint8_t version;
	uint8_t type;
	uint16_t request_id;
	uint16_t content_length;
	uint8_t padding_length;
	std::byte reserved;
	/*
	  std::byte content_data[content_length];
	  std::byte padding_data[padding_length];
	*/
};

static_assert(sizeof(fcgi_record_header) == 8, "Wrong FastCGI header size");

struct fcgi_begin_request {
	uint16_t role;
	uint8_t flags;
	uint8_t reserved[5];
};

static_assert(sizeof(fcgi_begin_request) == 8, "Wrong FastCGI packet size");
