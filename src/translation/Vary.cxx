// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Vary.hxx"
#include "translation/Response.hxx"
#include "translation/Protocol.hxx"
#include "strmap.hxx"
#include "memory/GrowingBuffer.hxx"
#include "http/HeaderWriter.hxx"
#include "AllocatorPtr.hxx"

#include <string.h>

using std::string_view_literals::operator""sv;

static const char *
translation_vary_name(TranslationCommand cmd)
{
	switch (cmd) {
	case TranslationCommand::SESSION:
		/* XXX need both "cookie2" and "cookie"? */
		return "cookie2";

	case TranslationCommand::LANGUAGE:
		return "accept-language";

	case TranslationCommand::AUTHORIZATION:
		return "authorization";

	case TranslationCommand::USER_AGENT:
		return "user-agent";

	default:
		return nullptr;
	}
}

static const char *
translation_vary_header(const TranslateResponse &response)
{
	static char buffer[256];
	char *p = buffer;

	for (const auto cmd : response.vary) {
		const char *name = translation_vary_name(cmd);
		if (name == nullptr)
			continue;

		if (p > buffer)
			*p++ = ',';

		size_t length = strlen(name);
		memcpy(p, name, length);
		p += length;
	}

	return p > buffer ? buffer : nullptr;
}

void
add_translation_vary_header(AllocatorPtr alloc, StringMap &headers,
			    const TranslateResponse &response)
{
	const char *value = translation_vary_header(response);
	if (value == nullptr)
		return;

	const char *old = headers.Get("vary");
	if (old != nullptr)
		value = alloc.Concat(old, ",", value);

	headers.Add(alloc, "vary", value);
}

void
write_translation_vary_header(GrowingBuffer &headers,
			      const TranslateResponse &response)
{
	bool active = false;
	for (const auto cmd : response.vary) {
		const char *name = translation_vary_name(cmd);
		if (name == nullptr)
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
