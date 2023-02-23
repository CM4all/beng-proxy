// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "fcgi/Protocol.hxx"

#include <cstdint>

#include <sys/types.h>
#include <string.h>

enum class HttpMethod : uint_least8_t;
enum class HttpStatus : uint_least16_t;
struct pool;
class StringMap;
struct fcgi_record_header;

struct FcgiRequest {
	uint16_t id;

	HttpMethod method;
	const char *uri;
	StringMap *headers;

	off_t length;
};

void
read_fcgi_header(struct fcgi_record_header *header);

void
read_fcgi_request(struct pool *pool, FcgiRequest *r);

void
discard_fcgi_request_body(FcgiRequest *r);

void
write_fcgi_stdout(const FcgiRequest *r,
		  const void *data, size_t length);

static inline void
write_fcgi_stdout_string(const FcgiRequest *r,
			 const char *data)
{
	write_fcgi_stdout(r, data, strlen(data));
}

void
write_fcgi_headers(const FcgiRequest *r, HttpStatus status,
		   StringMap *headers);

void
write_fcgi_end(const FcgiRequest *r);
