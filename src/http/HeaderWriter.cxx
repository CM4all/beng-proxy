// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "http/HeaderWriter.hxx"
#include "strmap.hxx"
#include "memory/GrowingBuffer.hxx"
#include "http/HeaderName.hxx"

#include <assert.h>
#include <string.h>

void
header_write_begin(GrowingBuffer &buffer, std::string_view name) noexcept
{
	assert(!name.empty());

	char *dest = (char *)buffer.Write(name.size() + 2);

	dest = std::copy(name.begin(), name.end(), dest);
	*dest++ = ':';
	*dest++ = ' ';
}

void
header_write_finish(GrowingBuffer &buffer) noexcept
{
	buffer.Write("\r\n");
}

void
header_write(GrowingBuffer &buffer,
	     std::string_view name, std::string_view value) noexcept
{
	assert(!name.empty());

	if (name.size() + value.size() >= 1024) [[unlikely]] {
		/* because GrowingBuffer::Write(size_t) can only deal with
		   small sizes, use this slightly slower code path for large
		   headers */
		buffer.Write(name);
		buffer.Write(": ");
		buffer.Write(value);
		buffer.Write("\r\n");
		return;
	}

	char *dest = (char *)buffer.Write(name.size() + 2 + value.size() + 2);
	dest = std::copy(name.begin(), name.end(), dest);
	*dest++ = ':';
	*dest++ = ' ';
	dest = std::copy(value.begin(), value.end(), dest);
	*dest++ = '\r';
	*dest = '\n';
}

void
headers_copy_one(const StringMap &in, GrowingBuffer &out,
		 const char *key) noexcept
{
	const char *value = in.Get(key);
	if (value != nullptr)
		header_write(out, key, value);
}

void
headers_copy(const StringMap &in, GrowingBuffer &out,
	     const char *const*keys) noexcept
{
	for (; *keys != nullptr; ++keys) {
		const char *value = in.Get(*keys);
		if (value != nullptr)
			header_write(out, *keys, value);
	}
}

void
headers_copy_all(const StringMap &in, GrowingBuffer &out) noexcept
{
	for (const auto &i : in)
		header_write(out, i.key, i.value);
}

void
headers_copy_most(const StringMap &in, GrowingBuffer &out) noexcept
{
	for (const auto &i : in)
		if (!http_header_is_hop_by_hop(i.key))
			header_write(out, i.key, i.value);
}

GrowingBuffer
headers_dup(const StringMap &in) noexcept
{
	GrowingBuffer out;
	headers_copy_most(in, out);
	return out;
}
