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

#pragma once

#include "Features.hxx"
#if TRANSLATION_ENABLE_HTTP
#include "net/SocketAddress.hxx"
#endif
#include "util/ConstBuffer.hxx"

#if TRANSLATION_ENABLE_HTTP
#include "http/Status.h"
#endif

#include <stddef.h>
#include <stdint.h>

enum class TranslationCommand : uint16_t;
struct TranslationLayoutItem;

struct TranslateRequest {
	const char *listener_tag = nullptr;

#if TRANSLATION_ENABLE_HTTP
	SocketAddress local_address = nullptr;
#endif

	const char *remote_host = nullptr;
	const char *host = nullptr;
	const char *alt_host = nullptr;
	const char *user_agent = nullptr;
	const char *accept_language = nullptr;

	/**
	 * The value of the "Authorization" HTTP request header.
	 */
	const char *authorization = nullptr;

	const char *uri = nullptr;
	const char *args = nullptr;
	const char *query_string = nullptr;
	const char *widget_type = nullptr;

#if TRANSLATION_ENABLE_SESSION
	ConstBuffer<void> session = nullptr;
#endif

	const char *param = nullptr;

	/**
	 * Mirror of the #TranslationCommand::LAYOUT packet.
	 */
	ConstBuffer<void> layout = nullptr;

	/**
	 * If #layout is set, then this is the #TranslationLayoutItem
	 * which matches the request.  This is not transmitted to the
	 * translation server, it is only evaluated by the
	 * #TranslationCache to look up cache items.  If this is
	 * nullptr, then there was no matching #TranslationLayoutItem.
	 */
	const TranslationLayoutItem *layout_item = nullptr;

	/**
	 * The payload of the #TRANSLATE_INTERNAL_REDIRECT packet.  If
	 * ConstBuffer::IsNull(), then no #TRANSLATE_INTERNAL_REDIRECT
	 * packet was received.
	 */
	ConstBuffer<void> internal_redirect = nullptr;

#if TRANSLATION_ENABLE_SESSION
	/**
	 * The payload of the CHECK packet.  If ConstBuffer::IsNull(),
	 * then no CHECK packet will be sent.
	 */
	ConstBuffer<void> check = nullptr;

	/**
	 * The payload of the AUTH packet.  If ConstBuffer::IsNull(),
	 * then no AUTH packet will be sent.
	 */
	ConstBuffer<void> auth = nullptr;
#endif

#if TRANSLATION_ENABLE_HTTP
	ConstBuffer<void> http_auth = nullptr;

	ConstBuffer<void> token_auth = nullptr;

	const char *auth_token = nullptr;

	/**
	 * The payload of the #TRANSLATE_WANT_FULL_URI packet.  If
	 * ConstBuffer::IsNull(), then no #TRANSLATE_WANT_FULL_URI packet
	 * was received.
	 */
	ConstBuffer<void> want_full_uri = nullptr;

	ConstBuffer<void> chain = nullptr;

	const char *chain_header = nullptr;
#endif

	ConstBuffer<TranslationCommand> want = nullptr;

	ConstBuffer<void> file_not_found = nullptr;

	ConstBuffer<void> content_type_lookup = nullptr;

	const char *suffix = nullptr;

	ConstBuffer<void> enotdir = nullptr;

	ConstBuffer<void> directory_index = nullptr;

#if TRANSLATION_ENABLE_HTTP
	ConstBuffer<void> error_document = nullptr;
#endif

	ConstBuffer<void> probe_path_suffixes = nullptr;
	const char *probe_suffix = nullptr;

	/**
	 * File contents.
	 */
	ConstBuffer<void> read_file = nullptr;

	const char *user = nullptr;

	const char *pool = nullptr;

#if TRANSLATION_ENABLE_HTTP
	http_status_t status = http_status_t(0);
#endif

	bool cron = false;

	/**
	 * Returns a name for this object to identify it in diagnostic
	 * messages.
	 */
	const char *GetDiagnosticName() const {
		if (uri != nullptr)
			return uri;

		if (widget_type != nullptr)
			return widget_type;

		if (suffix != nullptr)
			return suffix;

		return nullptr;
	}
};
