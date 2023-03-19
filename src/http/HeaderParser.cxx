// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "HeaderParser.hxx"
#include "pool/pool.hxx"
#include "strmap.hxx"
#include "memory/GrowingBuffer.hxx"
#include "http/HeaderName.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StaticFifoBuffer.hxx"
#include "util/StringCompare.hxx"
#include "util/StringSplit.hxx"
#include "util/StringStrip.hxx"
#include "AllocatorPtr.hxx"

#include <algorithm>

#include <string.h>

static constexpr bool
IsValidHeaderValueChar(char ch) noexcept
{
	return ch != '\0' && ch != '\n' && ch != '\r';
}

[[gnu::pure]]
static bool
IsValidHeaderValue(std::string_view value) noexcept
{
	for (char ch : value)
		if (!IsValidHeaderValueChar(ch))
			return false;

	return true;
}

bool
header_parse_line(AllocatorPtr alloc, StringMap &headers,
		  std::string_view line) noexcept
{
	auto [name, value] = Split(line, ':');

	if (value.data() == nullptr ||
	    !http_header_name_valid(name) ||
	    !IsValidHeaderValue(value)) [[unlikely]]
		return false;

	value = StripLeft(value);

	headers.Add(alloc, alloc.DupToLower(name), alloc.DupZ(value));
	return true;
}

void
header_parse_buffer(AllocatorPtr alloc, StringMap &headers,
		    GrowingBuffer &&_gb) noexcept
{
	GrowingBufferReader reader(std::move(_gb));

	StaticFifoBuffer<char, 4096> buffer;

	const auto *gb = &_gb;

	while (true) {
		/* copy gb to buffer */

		if (gb != nullptr) {
			auto w = buffer.Write();
			if (!w.empty()) {
				auto src = reader.Read();
				if (!src.empty()) {
					size_t nbytes = std::min(src.size(),
								 w.size());
					memcpy(w.data(), src.data(), nbytes);
					buffer.Append(nbytes);
					reader.Consume(nbytes);
				} else
					gb = nullptr;
			}
		}

		/* parse lines from the buffer */

		auto r = buffer.Read();
		if (r.empty() && gb == nullptr)
			break;

		const char *const src = (const char *)r.data();
		const char *p = src;
		const size_t length = r.size();

		while (true) {
			p = StripLeft(p, src + length);

			const char *eol = (const char *)memchr(p, '\n', src + length - p);
			if (eol == nullptr) {
				if (gb == nullptr)
					eol = src + length;
				else
					break;
			}

			while (eol > p && eol[-1] == '\r')
				--eol;

			header_parse_line(alloc, headers, {p, eol});
			p = eol + 1;
		}

		buffer.Consume(p - src);
	}
}

[[gnu::pure]]
static std::string_view
IsHeaderLineNamed(std::string_view line, std::string_view name) noexcept
{
	if (!SkipPrefix(line, name))
		return {};

	line = StripLeft(line);
	if (line.empty() || line.front() != ':')
		return {};

	return StripLeft(line.substr(1));
}

std::string_view
header_parse_find(std::string_view haystack, std::string_view name) noexcept
{
	while (!haystack.empty()) {
		auto [line, rest] = Split(haystack, '\n');

		if (auto value = IsHeaderLineNamed(line, name); value.data() != nullptr)
			return value;

		haystack = rest;
	}

	return {};
}
