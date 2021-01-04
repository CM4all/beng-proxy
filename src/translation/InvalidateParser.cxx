/*
 * Copyright 2007-2021 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#include "InvalidateParser.hxx"
#include "Request.hxx"
#include "util/ByteOrder.hxx"
#include "util/RuntimeError.hxx"
#include "pool/pool.hxx"

gcc_pure
static std::pair<const char *, const char *>
GetInvalidateNameValue(const TranslateRequest &request,
		       TranslationCommand command) noexcept
{
	switch (command) {
	case TranslationCommand::URI:
		return std::make_pair("uri", request.uri);

	case TranslationCommand::PARAM:
		return std::make_pair("param", request.param);

	case TranslationCommand::SESSION:
		return std::make_pair("session", "?");

	case TranslationCommand::LISTENER_TAG:
		return std::make_pair("listener_tag", request.listener_tag);

	case TranslationCommand::REMOTE_HOST:
		return std::make_pair("remote_host", request.remote_host);

	case TranslationCommand::HOST:
		return std::make_pair("host", request.host);

	case TranslationCommand::LANGUAGE:
		return std::make_pair("language", request.accept_language);

	case TranslationCommand::USER_AGENT:
		return std::make_pair("user_agent", request.user_agent);

	case TranslationCommand::UA_CLASS:
		return std::make_pair("ua_class", request.ua_class);

	case TranslationCommand::QUERY_STRING:
		return std::make_pair("query_string", request.query_string);

	case TranslationCommand::INTERNAL_REDIRECT:
		return std::make_pair("internal_redirect", "?");

	case TranslationCommand::ENOTDIR_:
		return std::make_pair("enotdir", "?");

	case TranslationCommand::USER:
		return std::make_pair("user", request.user);

	default:
		return std::make_pair("?", "?");
	}
}

std::string
TranslationInvalidateRequest::ToString() const noexcept
{
	std::string result;

	if (site != nullptr) {
		result.append("site=\"");
		result.append(site);
		result.push_back('"');
	}

	for (TranslationCommand command : commands) {
		auto nv = GetInvalidateNameValue(*this, command);

		if (!result.empty())
			result.push_back(' ');

		result.append(nv.first);
		result.push_back('=');
		result.push_back('"');
		result.append(nv.second);
		result.push_back('"');
	}

	return result;
}

static void
apply_translation_packet(TranslateRequest &request,
			 enum TranslationCommand command,
			 const char *payload, size_t payload_length)
{
	switch (command) {
	case TranslationCommand::URI:
		request.uri = payload;
		break;

	case TranslationCommand::PARAM:
		request.param = payload;
		break;

	case TranslationCommand::SESSION:
		request.session = { payload, payload_length };
		break;

	case TranslationCommand::LISTENER_TAG:
		request.listener_tag = payload;
		break;

		/* XXX
		   case TranslationCommand::LOCAL_ADDRESS:
		   request.local_address = payload;
		   break;
		*/

	case TranslationCommand::REMOTE_HOST:
		request.remote_host = payload;
		break;

	case TranslationCommand::HOST:
		request.host = payload;
		break;

	case TranslationCommand::LANGUAGE:
		request.accept_language = payload;
		break;

	case TranslationCommand::USER_AGENT:
		request.user_agent = payload;
		break;

	case TranslationCommand::UA_CLASS:
		request.ua_class = payload;
		break;

	case TranslationCommand::QUERY_STRING:
		request.query_string = payload;
		break;

	case TranslationCommand::INTERNAL_REDIRECT:
		request.internal_redirect = { payload, payload_length };
		break;

	case TranslationCommand::ENOTDIR_:
		request.enotdir = { payload, payload_length };
		break;

	case TranslationCommand::USER:
		request.user = payload;
		break;

	default:
		/* unsupported */
		throw FormatRuntimeError("Unsupported packet: %u", unsigned(command));
	}
}

TranslationInvalidateRequest
ParseTranslationInvalidateRequest(struct pool &pool,
				  const void *data, size_t length)
{
	TranslationInvalidateRequest request;

	if (length % 4 != 0)
		/* must be padded */
		throw std::runtime_error("Not padded");

	while (length > 0) {
		const auto *header = (const TranslationHeader *)data;
		if (length < sizeof(*header))
			throw std::runtime_error("Partial header");

		size_t payload_length = FromBE16(header->length);
		const auto command =
			TranslationCommand(FromBE16(uint16_t(header->command)));

		data = header + 1;
		length -= sizeof(*header);

		if (length < payload_length)
			throw std::runtime_error("Truncated payload");

		const char *payload = payload_length > 0
			? p_strndup(&pool, (const char *)data, payload_length)
			: "";
		if (command == TranslationCommand::SITE)
			request.site = payload;
		else {
			apply_translation_packet(request, command, payload,
						 payload_length);

			if (!request.commands.checked_append(command))
				throw std::runtime_error("Too many commands");
		}

		payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

		data = (const char *)data + payload_length;
		length -= payload_length;
	}

	return request;
}
