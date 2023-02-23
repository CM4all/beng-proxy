// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "InvalidateParser.hxx"
#include "Request.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "net/control/Padding.hxx"
#include "util/ByteOrder.hxx"
#include "util/SpanCast.hxx"
#include "AllocatorPtr.hxx"

[[gnu::pure]]
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

	case TranslationCommand::REALM_SESSION:
		return std::make_pair("realm_session", "?");

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
		request.session = { (const std::byte *)payload, payload_length };
		break;

	case TranslationCommand::REALM_SESSION:
		request.realm_session = { (const std::byte *)payload, payload_length };
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

	case TranslationCommand::QUERY_STRING:
		request.query_string = payload;
		break;

	case TranslationCommand::INTERNAL_REDIRECT:
		request.internal_redirect = { (const std::byte *)payload, payload_length };
		break;

	case TranslationCommand::ENOTDIR_:
		request.enotdir = { (const std::byte *)payload, payload_length };
		break;

	case TranslationCommand::USER:
		request.user = payload;
		break;

	default:
		/* unsupported */
		throw FmtRuntimeError("Unsupported packet: {}", unsigned(command));
	}
}

TranslationInvalidateRequest
ParseTranslationInvalidateRequest(AllocatorPtr alloc,
				  std::span<const std::byte> p)
{
	TranslationInvalidateRequest request;

	if (!BengProxy::IsControlSizePadded(p.size()))
		/* must be padded */
		throw std::runtime_error("Not padded");

	while (!p.empty()) {
		const auto *header = (const TranslationHeader *)
			(const void *)p.data();
		if (p.size() < sizeof(*header))
			throw std::runtime_error("Partial header");

		size_t payload_length = FromBE16(header->length);
		const auto command =
			TranslationCommand(FromBE16(uint16_t(header->command)));

		p = p.subspan(sizeof(*header));

		if (p.size() < payload_length)
			throw std::runtime_error("Truncated payload");

		const char *payload = payload_length > 0
			? alloc.DupZ(ToStringView(p.first(payload_length)))
			: "";
		if (command == TranslationCommand::SITE)
			request.site = payload;
		else {
			apply_translation_packet(request, command, payload,
						 payload_length);

			if (request.commands.full())
				throw std::runtime_error("Too many commands");

			request.commands.push_back(command);
		}

		payload_length = ((payload_length + 3) | 3) - 3; /* apply padding */

		p = p.subspan(payload_length);
	}

	return request;
}
