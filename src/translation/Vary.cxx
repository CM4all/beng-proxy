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

using std::string_view_literals::operator""sv;

static constexpr std::string_view
translation_vary_name(TranslationCommand cmd) noexcept
{
	switch (cmd) {
	case TranslationCommand::SESSION:
		/* XXX need both "cookie2" and "cookie"? */
		return "cookie2"sv;

	case TranslationCommand::LANGUAGE:
		return "accept-language"sv;

	case TranslationCommand::AUTHORIZATION:
		return "authorization"sv;

	case TranslationCommand::USER_AGENT:
		return "user-agent"sv;

	default:
		return {};
	}
}

static std::string_view
translation_vary_header(const TranslateResponse &response) noexcept
{
	static char buffer[256];
	char *p = buffer;

	for (const auto cmd : response.vary) {
		const std::string_view name = translation_vary_name(cmd);
		if (name.empty())
			continue;

		if (p > buffer)
			*p++ = ',';

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
	for (const auto cmd : response.vary) {
		const std::string_view name = translation_vary_name(cmd);
		if (name.empty())
			continue;

		if (active) {
			headers.Write(","sv);
		} else {
			active = true;
			header_write_begin(headers, "vary");
		}

		headers.Write(name);
	}

	if (active)
		header_write_finish(headers);
}
