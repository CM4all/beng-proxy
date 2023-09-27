// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "fcgi_server.hxx"
#include "pool/pool.hxx"
#include "http/Method.hxx"
#include "system/Error.hxx"
#include "util/ByteOrder.hxx"
#include "util/CharUtil.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"

#include <stdexcept>

#include <stdio.h>
#include <sys/socket.h>

struct fcgi_record_header
FcgiServer::ReadHeader()
{
	struct fcgi_record_header header;
	ReadFullRaw(std::as_writable_bytes(std::span{&header, 1}));

	if (header.version != FCGI_VERSION_1)
		throw std::runtime_error{"Wrong FastCGI protocol version"};

	return header;
}

inline std::pair<struct fcgi_begin_request, uint_least16_t>
FcgiServer::ReadBeginRequest()
{
	const auto header = ReadHeader();
	if (header.type != FCGI_BEGIN_REQUEST)
		throw std::runtime_error{"BEGIN_REQUEST expected"};

	struct fcgi_begin_request begin;
	if (FromBE16(header.content_length) != sizeof(begin))
		throw std::runtime_error{"Malformed BEGIN_REQUEST"};

	ReadFullRaw(std::as_writable_bytes(std::span{&begin, 1}));
	DiscardRaw(header.padding_length);

	return {begin, header.request_id};
}

std::byte
FcgiServer::ReadByte(std::size_t &remaining)
{
	if (remaining == 0)
		throw std::runtime_error{"Premature end of packet"};

	std::byte value;
	ReadFullRaw({&value, 1});
	--remaining;
	return value;
}

std::size_t
FcgiServer::ReadLength(std::size_t &remaining)
{
	const uint_least8_t a = (uint_least8_t)ReadByte(remaining);
	if (a < 0x80)
		return a;

	const uint_least8_t b = (uint_least8_t)ReadByte(remaining);
	const uint_least8_t c = (uint_least8_t)ReadByte(remaining);
	const uint_least8_t d = (uint_least8_t)ReadByte(remaining);

	return ((a & 0x7f) << 24) | (b << 16) | (c << 8) | d;
}

static void
handle_fcgi_param(struct pool *pool, FcgiRequest *r,
		  const char *name, const char *value)
{
	if (strcmp(name, "REQUEST_METHOD") == 0) {
		if (strcmp(value, "HEAD") == 0)
			r->method = HttpMethod::HEAD;
		else if (strcmp(value, "POST") == 0)
			r->method = HttpMethod::POST;
	} else if (strcmp(name, "REQUEST_URI") == 0) {
		r->uri = p_strdup(pool, value);
	} else if (memcmp(name, "HTTP_", 5) == 0 && name[5] != 0) {
		char *p = p_strdup(pool, name + 5);

		for (char *q = p; *q != 0; ++q) {
			if (*q == '_')
				*q = '-';
			else if (IsUpperAlphaASCII(*q))
				*q += 'a' - 'A';
		}

		r->headers.Add(*pool, p, p_strdup(pool, value));
	}
}

void
FcgiServer::ReadParams(struct pool &pool, FcgiRequest &r)
{
	r.method = HttpMethod::GET;
	r.uri = nullptr;

	char name[1024], value[8192];
	while (true) {
		const auto header = ReadHeader();

		if (header.type != FCGI_PARAMS)
			throw std::runtime_error{"PARAMS expected"};

		if (header.request_id != r.id)
			throw std::runtime_error{"Malformed PARAMS"};

		size_t remaining = FromBE16(header.content_length);
		if (remaining == 0)
			break;

		while (remaining > 0) {
			const std::size_t name_length = ReadLength(remaining);
			const std::size_t value_length = ReadLength(remaining);

			if (name_length >= sizeof(name) || value_length >= sizeof(value) ||
			    name_length + value_length > remaining)
				throw std::runtime_error{"Malformed PARAMS"};

			ReadFullRaw(std::as_writable_bytes(std::span{name}.first(name_length)));
			name[name_length] = 0;
			remaining -= name_length;

			ReadFullRaw(std::as_writable_bytes(std::span{value}.first(value_length)));
			value[value_length] = 0;
			remaining -= value_length;

			handle_fcgi_param(&pool, &r, name, value);
		}

		DiscardRaw(header.padding_length);
	}
}

FcgiRequest
FcgiServer::ReadRequest(struct pool &pool)
{
	const auto [begin, request_id] = ReadBeginRequest();
	if (FromBE16(begin.role) != FCGI_RESPONDER)
		throw std::runtime_error{"role==RESPONDER expected"};

	FcgiRequest r;
	r.id = request_id;

	ReadParams(pool, r);

	const char *content_length = r.headers.Remove("content-length");
	r.length = content_length != nullptr
		? strtol(content_length, nullptr, 10)
		: -1;

	if (content_length == nullptr) {
		struct fcgi_record_header header;
		ssize_t nbytes = socket.Receive(std::as_writable_bytes(std::span{&header, 1}),
						MSG_DONTWAIT|MSG_PEEK);
		if (nbytes == (ssize_t)sizeof(header) &&
		    header.version == FCGI_VERSION_1 &&
		    header.type == FCGI_STDIN &&
		    header.content_length == 0)
			r.length = 0;
	}

	return r;
}

void
FcgiServer::DiscardRequestBody(const FcgiRequest &r)
{
	while (true) {
		const auto header = ReadHeader();

		if (header.type != FCGI_STDIN)
			throw std::runtime_error{"STDIN expected"};

		if (header.request_id != r.id)
			throw std::runtime_error{"Malformed STDIN"};

		std::size_t content_length = FromBE16(header.content_length);
		DiscardRaw(content_length + header.padding_length);

		if (content_length == 0)
			break;
	}
}

std::size_t
FcgiServer::ReadRaw(std::span<std::byte> dest)
{
	auto nbytes = socket.Receive(dest);
	if (nbytes < 0)
		throw MakeErrno("Failed to receive");

	return nbytes;
}

std::size_t
FcgiServer::ReadAllRaw(std::span<std::byte> dest)
{
	auto nbytes = socket.Receive(dest, MSG_WAITALL);
	if (nbytes < 0)
		throw MakeErrno("Failed to receive");

	return nbytes;
}

void
FcgiServer::ReadFullRaw(std::span<std::byte> dest)
{
	while (!dest.empty()) {
		std::size_t nbytes = ReadAllRaw(dest);
		if (nbytes == 0)
			throw std::runtime_error{"Peer closed the socket prematurely"};

		dest = dest.subspan(nbytes);
	}
}

void
FcgiServer::DiscardRaw(std::size_t size)
{
	while (size > 0) {
		std::byte buffer[4096];

		std::span<std::byte> dest = buffer;
		if (size < dest.size())
			dest = dest.first(size);

		std::size_t nbytes = ReadAllRaw(dest);
		if (nbytes == 0)
			throw std::runtime_error{"Peer closed the socket prematurely"};

		size -= nbytes;
	}
}

std::size_t
FcgiServer::WriteRaw(std::span<const std::byte> src)
{
	auto nbytes = socket.Send(src);
	if (nbytes < 0)
		throw MakeErrno("Failed to send");

	return nbytes;
}

void
FcgiServer::WriteFullRaw(std::span<const std::byte> src)
{
	while (!src.empty()) {
		std::size_t nbytes = WriteRaw(src);
		src = src.subspan(nbytes);
	}
}

void
FcgiServer::WriteRecord(const FcgiRequest &r, uint8_t type, std::string_view payload)
{
	WriteHeader({
		.version = FCGI_VERSION_1,
		.type = type,
		.request_id = r.id,
		.content_length = ToBE16(payload.size()),
	});

	WriteFullRaw(AsBytes(payload));
}

void
FcgiServer::WriteResponseHeaders(const FcgiRequest &r, HttpStatus status,
				 const StringMap &headers)
{
	char buffer[8192], *p = buffer;
	p += sprintf(p, "status: %u\n", static_cast<unsigned>(status));

	for (const auto &i : headers)
		p += sprintf(p, "%s: %s\n", i.key, i.value);

	p += sprintf(p, "\n");

	WriteStdout(r, {buffer, std::size_t(p - buffer)});
}

void
FcgiServer::EndResponse(const FcgiRequest &r)
{
	WriteHeader({
		.version = FCGI_VERSION_1,
		.type = FCGI_END_REQUEST,
		.request_id = r.id,
	});
}

void
FcgiServer::MirrorRaw(std::size_t size)
{
	while (size > 0) {
		std::byte buffer[4096];

		std::span<std::byte> dest = buffer;
		if (size < dest.size())
			dest = dest.first(size);

		auto nbytes = ReadAllRaw(dest);
		if (nbytes == 0)
			throw std::runtime_error{"Peer closed the socket prematurely"};

		WriteFullRaw(dest.first(nbytes));

		size -= nbytes;
	}
}
