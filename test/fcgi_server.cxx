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
#include "util/StringAPI.hxx"
#include "AllocatorPtr.hxx"

#include <stdexcept>

#include <stdio.h>
#include <stdlib.h> // for strtol()
#include <sys/socket.h>

FcgiRecordHeader
FcgiServer::ReadHeader()
{
	FcgiRecordHeader header;
	ReadFullRaw(std::as_writable_bytes(std::span{&header, 1}));

	if (header.version != FCGI_VERSION_1)
		throw std::runtime_error{"Wrong FastCGI protocol version"};

	return header;
}

inline std::pair<FcgiBeginRequest, uint_least16_t>
FcgiServer::ReadBeginRequest()
{
	const auto header = ReadHeader();
	if (header.type != FcgiRecordType::BEGIN_REQUEST)
		throw std::runtime_error{"BEGIN_REQUEST expected"};

	FcgiBeginRequest begin;
	if (header.content_length != sizeof(begin))
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
	if (StringIsEqual(name, "REQUEST_METHOD")) {
		if (StringIsEqual(value, "HEAD"))
			r->method = HttpMethod::HEAD;
		else if (StringIsEqual(value, "POST"))
			r->method = HttpMethod::POST;
	} else if (StringIsEqual(name, "REQUEST_URI")) {
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

		if (header.type != FcgiRecordType::PARAMS)
			throw std::runtime_error{"PARAMS expected"};

		if (header.request_id != r.id)
			throw std::runtime_error{"Malformed PARAMS"};

		size_t remaining = header.content_length;
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
	if (begin.role != static_cast<uint16_t>(FcgiRole::RESPONDER))
		throw std::runtime_error{"role==RESPONDER expected"};

	FcgiRequest r;
	r.id = request_id;

	ReadParams(pool, r);

	const char *content_length = r.headers.Remove("content-length");
	r.length = content_length != nullptr
		? strtol(content_length, nullptr, 10)
		: -1;

	if (content_length == nullptr) {
		FcgiRecordHeader header;
		ssize_t nbytes = socket.Receive(std::as_writable_bytes(std::span{&header, 1}),
						MSG_DONTWAIT|MSG_PEEK);
		if (nbytes == (ssize_t)sizeof(header) &&
		    header.version == FCGI_VERSION_1 &&
		    header.type == FcgiRecordType::STDIN &&
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

		if (header.type != FcgiRecordType::STDIN)
			throw std::runtime_error{"STDIN expected"};

		if (header.request_id != r.id)
			throw std::runtime_error{"Malformed STDIN"};

		std::size_t content_length = header.content_length;
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

void
FcgiServer::FlushOutput()
{
	while (true) {
		const auto r = output_buffer.Read();
		if (r.empty())
			return;

		auto nbytes = socket.Send(r);
		if (nbytes < 0)
			throw MakeErrno("Failed to send");

		output_buffer.Consume(nbytes);
	}
}

std::size_t
FcgiServer::WriteRaw(std::span<const std::byte> src)
{
	auto w = output_buffer.Write();
	if (w.empty())
		FlushOutput();

	if (w.size() < src.size())
		src = src.first(w.size());

	std::copy(src.begin(), src.end(), w.begin());
	output_buffer.Append(src.size());

	return src.size();
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
FcgiServer::WriteZero(std::size_t size)
{
	static constexpr std::byte zero[4096]{};

	while (size > 0) {
		std::span<const std::byte> src{zero};
		if (src.size() > size)
			src = src.first(size);

		std::size_t nbytes = WriteRaw(src);
		size -= nbytes;
	}
}

void
FcgiServer::WriteRecord(const FcgiRequest &r, FcgiRecordType type,
			std::string_view payload,
			std::size_t padding)
{
	assert(payload.size() < 0x10000);
	assert(padding < 0x100);

	WriteHeader({
		.version = FCGI_VERSION_1,
		.type = type,
		.request_id = r.id,
		.content_length = payload.size(),
		.padding_length = static_cast<uint8_t>(padding),
	});

	WriteFullRaw(AsBytes(payload));
	WriteZero(padding);
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
	static constexpr FcgiEndRequest end_request{
		.protocol_status = static_cast<uint8_t>(FcgiProtocolStatus::REQUEST_COMPLETE),
	};

	WriteHeader({
		.version = FCGI_VERSION_1,
		.type = FcgiRecordType::END_REQUEST,
		.request_id = r.id,
		.content_length = sizeof(end_request),
	});

	WriteFullRaw(ReferenceAsBytes(end_request));
}

void
FcgiServer::MirrorRaw(std::size_t size)
{
	while (size > 0) {
		std::byte buffer[4096];

		std::span<std::byte> dest = buffer;
		if (size < dest.size())
			dest = dest.first(size);

		auto nbytes = ReadRaw(dest);
		if (nbytes == 0)
			throw std::runtime_error{"Peer closed the socket prematurely"};

		WriteFullRaw(dest.first(nbytes));
		FlushOutput();

		size -= nbytes;
	}
}
