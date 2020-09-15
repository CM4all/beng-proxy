/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "Marshal.hxx"
#include "Request.hxx"
#include "translation/Protocol.hxx"
#include "net/ToString.hxx"
#include "util/RuntimeError.hxx"
#include "util/StringView.hxx"

void
TranslationMarshaller::Write(TranslationCommand command,
			     ConstBuffer<void> payload)
{
	if (payload.size >= 0xffff)
		throw FormatRuntimeError("payload for translate command %u too large",
					 command);

	TranslationHeader header;
	header.length = (uint16_t)payload.size;
	header.command = command;

	buffer.Write(&header, sizeof(header));

	if (!payload.empty())
		buffer.Write(payload.data, payload.size);
}

void
TranslationMarshaller::Write(TranslationCommand command,
			     const char *payload)
{
	Write(command, StringView(payload));
}

void
TranslationMarshaller::Write(TranslationCommand command,
			     TranslationCommand command_string,
			     SocketAddress address)
{
	assert(!address.IsNull());

	Write(command, ConstBuffer<void>(address.GetAddress(), address.GetSize()));

	char address_string[1024];

	if (ToString(address_string, sizeof(address_string), address))
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

	if (request.status != 0)
		m.Write16(TranslationCommand::STATUS, request.status);

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
	m.WriteOptional(TranslationCommand::UA_CLASS, request.ua_class);
	m.WriteOptional(TranslationCommand::LANGUAGE, request.accept_language);
	m.WriteOptional(TranslationCommand::AUTHORIZATION, request.authorization);
	m.WriteOptional(TranslationCommand::URI, request.uri);
	m.WriteOptional(TranslationCommand::ARGS, request.args);
	m.WriteOptional(TranslationCommand::QUERY_STRING, request.query_string);
	m.WriteOptional(TranslationCommand::WIDGET_TYPE, request.widget_type);
	m.WriteOptional(TranslationCommand::SESSION, request.session);
	m.WriteOptional(TranslationCommand::INTERNAL_REDIRECT,
			request.internal_redirect);
	m.WriteOptional(TranslationCommand::CHECK, request.check);
	m.WriteOptional(TranslationCommand::AUTH, request.auth);
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
	m.Write(TranslationCommand::END);

	return m.Commit();
}
