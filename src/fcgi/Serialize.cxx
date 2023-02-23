// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Serialize.hxx"
#include "Protocol.hxx"
#include "memory/GrowingBuffer.hxx"
#include "strmap.hxx"
#include "util/CharUtil.hxx"
#include "util/ByteOrder.hxx"

#include <cassert>
#include <cstdint>

FcgiRecordSerializer::FcgiRecordSerializer(GrowingBuffer &_buffer,
					   uint8_t type,
					   uint16_t request_id_be) noexcept
	:buffer(_buffer),
	 header((struct fcgi_record_header *)buffer.Write(sizeof(*header)))
{
	header->version = FCGI_VERSION_1;
	header->type = type;
	header->request_id = request_id_be;
	header->padding_length = 0;
	header->reserved = 0;
}

void
FcgiRecordSerializer::Commit(size_t content_length) noexcept
{
	assert(content_length < (1 << 16));
	header->content_length = ToBE16(content_length);
}

static size_t
fcgi_serialize_length(GrowingBuffer &gb, std::size_t length) noexcept
{
	if (length < 0x80) {
		uint8_t buffer = (uint8_t)length;
		gb.WriteT(buffer);
		return sizeof(buffer);
	} else {
		/* XXX 31 bit overflow? */
		uint32_t buffer = ToBE32(length | 0x80000000);
		gb.WriteT(buffer);
		return sizeof(buffer);
	}
}

static size_t
fcgi_serialize_pair(GrowingBuffer &gb, std::string_view name,
		    std::string_view value) noexcept
{
	std::size_t size = fcgi_serialize_length(gb, name.size());
	size += fcgi_serialize_length(gb, value.size());

	gb.Write(name);
	gb.Write(value);

	return size + name.size() + value.size();
}

FcgiParamsSerializer::FcgiParamsSerializer(GrowingBuffer &_buffer,
					   uint16_t request_id_be) noexcept
	:record(_buffer, FCGI_PARAMS, request_id_be) {}

FcgiParamsSerializer &
FcgiParamsSerializer::operator()(std::string_view name,
				 std::string_view value) noexcept
{
	content_length += fcgi_serialize_pair(record.GetBuffer(), name, value);
	return *this;
}

void
FcgiParamsSerializer::Headers(const StringMap &headers) noexcept
{
	char buffer[512] = "HTTP_";

	for (const auto &pair : headers) {
		size_t i;

		for (i = 0; 5 + i < sizeof(buffer) - 1 && pair.key[i] != 0; ++i) {
			if (IsLowerAlphaASCII(pair.key[i]))
				buffer[5 + i] = (char)(pair.key[i] - 'a' + 'A');
			else if (IsUpperAlphaASCII(pair.key[i]) ||
				 IsDigitASCII(pair.key[i]))
				buffer[5 + i] = pair.key[i];
			else
				buffer[5 + i] = '_';
		}

		(*this)({buffer, 5 + i}, pair.value);
	}
}
