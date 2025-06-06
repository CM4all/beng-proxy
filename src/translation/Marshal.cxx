// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Marshal.hxx"
#include "Request.hxx"
#include "Layout.hxx"
#include "translation/Protocol.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "net/FormatAddress.hxx"
#include "util/SpanCast.hxx"

void
TranslationMarshaller::Write(TranslationCommand command,
			     std::span<const std::byte> payload)
{
	if (payload.size() >= 0xffff)
		throw FmtRuntimeError("payload for translate command {} too large",
				      (unsigned)command);

	TranslationHeader header;
	header.length = (uint16_t)payload.size();
	header.command = command;

	buffer.WriteT(header);

	if (!payload.empty())
		buffer.Write(payload);
}

void
TranslationMarshaller::Write(TranslationCommand command,
			     std::string_view payload)
{
	Write(command, AsBytes(payload));
}

void
TranslationMarshaller::Write(TranslationCommand command,
			     TranslationCommand command_string,
			     SocketAddress address)
{
	assert(!address.IsNull());

	Write(command, address);

	char address_string[1024];

	if (ToString(address_string, address))
		Write(command_string, address_string);
}

void
TranslationMarshaller::WriteOptional(TranslationCommand command,
				     TranslationCommand command_string,
				     SocketAddress address)
{
	if (!address.IsNull())
		Write(command, command_string, address);
}

GrowingBuffer
MarshalTranslateRequest(uint8_t PROTOCOL_VERSION,
			const TranslateRequest &request)
{
	TranslationMarshaller m;

	m.WriteT(TranslationCommand::BEGIN, PROTOCOL_VERSION);
	m.WriteOptional(TranslationCommand::ERROR_DOCUMENT,
			request.error_document);

	if (request.status != HttpStatus{})
		m.Write16(TranslationCommand::STATUS,
			  static_cast<uint16_t>(request.status));

	m.WriteOptional(TranslationCommand::LISTENER_TAG,
			request.listener_tag);
	m.WriteOptional(TranslationCommand::LOCAL_ADDRESS,
			TranslationCommand::LOCAL_ADDRESS_STRING,
			request.local_address);
	m.WriteOptional(TranslationCommand::REMOTE_HOST,
			request.remote_host);
	m.WriteOptional(TranslationCommand::HOST, request.host);
	m.WriteOptional(TranslationCommand::ALT_HOST, request.alt_host);
	m.WriteOptional(TranslationCommand::USER_AGENT, request.user_agent);
	m.WriteOptional(TranslationCommand::LANGUAGE, request.accept_language);
	m.WriteOptional(TranslationCommand::AUTHORIZATION, request.authorization);
	m.WriteOptional(TranslationCommand::URI, request.uri);
	m.WriteOptional(TranslationCommand::ARGS, request.args);
	m.WriteOptional(TranslationCommand::QUERY_STRING, request.query_string);
	m.WriteOptional(TranslationCommand::WIDGET_TYPE, request.widget_type);
	m.WriteOptional(TranslationCommand::SESSION, request.session);
	m.WriteOptional(TranslationCommand::REALM_SESSION,
			request.realm_session);
	m.WriteOptional(TranslationCommand::RECOVER_SESSION,
			request.recover_session);
	m.WriteOptional(TranslationCommand::LAYOUT, request.layout);

	if (request.layout_item != nullptr) {
		switch (request.layout_item->GetType()) {
		case TranslationLayoutItem::Type::BASE:
			m.Write(TranslationCommand::BASE,
				request.layout_item->value);
			break;

		case TranslationLayoutItem::Type::REGEX:
			m.Write(TranslationCommand::REGEX,
				request.layout_item->value);
			break;
		}
	}

	m.WriteOptional(TranslationCommand::INTERNAL_REDIRECT,
			request.internal_redirect);
	m.WriteOptional(TranslationCommand::CHECK, request.check);
	m.WriteOptional(TranslationCommand::CHECK_HEADER,
			request.check_header);
	m.WriteOptional(TranslationCommand::AUTH, request.auth);
	m.WriteOptional(TranslationCommand::HTTP_AUTH, request.http_auth);
	m.WriteOptional(TranslationCommand::TOKEN_AUTH, request.token_auth);
	m.WriteOptional(TranslationCommand::AUTH_TOKEN, request.auth_token);
	m.WriteOptional(TranslationCommand::MOUNT_LISTEN_STREAM,
			request.mount_listen_stream);
	m.WriteOptional(TranslationCommand::WANT_FULL_URI, request.want_full_uri);
	m.WriteOptional(TranslationCommand::CHAIN, request.chain);
	m.WriteOptional(TranslationCommand::CHAIN_HEADER, request.chain_header);
	m.WriteOptional(TranslationCommand::WANT, request.want);
	m.WriteOptional(TranslationCommand::FILE_NOT_FOUND,
			request.file_not_found);
	m.WriteOptional(TranslationCommand::CONTENT_TYPE_LOOKUP,
			request.content_type_lookup);
	m.WriteOptional(TranslationCommand::SUFFIX, request.suffix);
	m.WriteOptional(TranslationCommand::ENOTDIR_, request.enotdir);
	m.WriteOptional(TranslationCommand::DIRECTORY_INDEX,
			request.directory_index);
	m.WriteOptional(TranslationCommand::PARAM, request.param);
	m.WriteOptional(TranslationCommand::PROBE_PATH_SUFFIXES,
			request.probe_path_suffixes);
	m.WriteOptional(TranslationCommand::PROBE_SUFFIX,
			request.probe_suffix);
	m.WriteOptional(TranslationCommand::READ_FILE,
			request.read_file);
	m.WriteOptional(TranslationCommand::USER, request.user);
	m.WriteOptional(TranslationCommand::POOL, request.pool);

	if (request.path_exists)
		m.Write(TranslationCommand::PATH_EXISTS);

	m.Write(TranslationCommand::END);

	return m.Commit();
}
