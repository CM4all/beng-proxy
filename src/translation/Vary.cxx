// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Vary.hxx"
#include "translation/Response.hxx"
#include "translation/Protocol.hxx"
#include "strmap.hxx"
#include "memory/GrowingBuffer.hxx"
#include "http/CommonHeaders.hxx"
#include "http/HeaderWriter.hxx"
#include "AllocatorPtr.hxx"

#include <algorithm> // for std::copy()
#include <array>

using std::string_view_literals::operator""sv;

static constexpr struct {
	TranslationCommand cmd;
	std::string_view http_header;
} translation_vary_headers[] = {
	{TranslationCommand::SESSION, "cookie2"sv}, // TODO need both "cookie2" and "cookie"?
	{TranslationCommand::LANGUAGE, "accept-language"sv},
	{TranslationCommand::AUTHORIZATION, "authorization"sv},
	{TranslationCommand::USER_AGENT, "user-agent"sv},
};

static constexpr auto
CollectTranslationVary(std::span<const TranslationCommand> vary) noexcept
{
	static constexpr std::size_t N = std::size(translation_vary_headers);
	std::array<bool, N> result{};

	for (const auto cmd : vary) {
		for (std::size_t i = 0; i < N; ++i) {
			if (cmd == translation_vary_headers[i].cmd) {
				result[i] = true;
				break;
			}
		}
	}

	return result;
}

static std::string_view
translation_vary_header(const TranslateResponse &response) noexcept
{
	static char buffer[256];
	char *p = buffer;

	const auto vary = CollectTranslationVary(response.vary);
	for (std::size_t i = 0; i < vary.size(); ++i) {
		if (!vary[i])
			continue;

		if (p > buffer)
			*p++ = ',';

		const std::string_view name = translation_vary_headers[i].http_header;
		assert(!name.empty());

		p = std::copy(name.begin(), name.end(), p);
	}

	return {buffer, p};
}

void
add_translation_vary_header(AllocatorPtr alloc, StringMap &headers,
			    const TranslateResponse &response) noexcept
{
	if (const std::string_view value = translation_vary_header(response);
	    !value.empty())
		headers.Add(alloc, vary_header, alloc.DupZ(value));
}

void
write_translation_vary_header(GrowingBuffer &headers,
			      const TranslateResponse &response) noexcept
{
	bool active = false;

	const auto vary = CollectTranslationVary(response.vary);
	for (std::size_t i = 0; i < vary.size(); ++i) {
		if (!vary[i])
			continue;

		if (active) {
			headers.Write(","sv);
		} else {
			active = true;
			header_write_begin(headers, "vary");
		}

		const std::string_view name = translation_vary_headers[i].http_header;
		assert(!name.empty());
		headers.Write(name);
	}

	if (active)
		header_write_finish(headers);
}
