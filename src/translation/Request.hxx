// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "Features.hxx"
#if TRANSLATION_ENABLE_HTTP
#include "net/SocketAddress.hxx"
#endif

#include <span>

#include <stddef.h>
#include <stdint.h>

enum class HttpStatus : uint_least16_t;
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
	std::span<const std::byte> session = {};
	std::span<const std::byte> realm_session = {};

	const char *recover_session = nullptr;
#endif

	const char *param = nullptr;

	/**
	 * Mirror of the #TranslationCommand::LAYOUT packet.
	 */
	std::span<const std::byte> layout = {};

	/**
	 * If #layout is set, then this is the #TranslationLayoutItem
	 * which matches the request.  This is transmitted to the
	 * translation server, but also evaluated by the
	 * #TranslationCache to look up cache items.  If this is
	 * nullptr, then there was no matching #TranslationLayoutItem.
	 */
	const TranslationLayoutItem *layout_item = nullptr;

	/**
	 * The payload of the #TRANSLATE_INTERNAL_REDIRECT packet.  If
	 * nullptr, then no #TRANSLATE_INTERNAL_REDIRECT
	 * packet was received.
	 */
	std::span<const std::byte> internal_redirect = {};

#if TRANSLATION_ENABLE_SESSION
	/**
	 * The payload of the CHECK packet.  If nullptr,
	 * then no CHECK packet will be sent.
	 */
	std::span<const std::byte> check = {};

	const char *check_header = nullptr;

	/**
	 * The payload of the AUTH packet.  If nullptr,
	 * then no AUTH packet will be sent.
	 */
	std::span<const std::byte> auth = {};
#endif

#if TRANSLATION_ENABLE_HTTP
	std::span<const std::byte> http_auth = {};

	std::span<const std::byte> token_auth = {};

	const char *auth_token = nullptr;

	/**
	 * The payload of the #TRANSLATE_WANT_FULL_URI packet.  If
	 * nullptr, then no #TRANSLATE_WANT_FULL_URI packet
	 * was received.
	 */
	std::span<const std::byte> want_full_uri = {};

	std::span<const std::byte> chain = {};

	const char *chain_header = nullptr;
#endif

	std::span<const TranslationCommand> want = {};

	std::span<const std::byte> file_not_found = {};

	std::span<const std::byte> content_type_lookup = {};

	const char *suffix = nullptr;

	std::span<const std::byte> enotdir = {};

	std::span<const std::byte> directory_index = {};

#if TRANSLATION_ENABLE_HTTP
	std::span<const std::byte> error_document = {};
#endif

#if TRANSLATION_ENABLE_SPAWN
	std::span<const std::byte> mount_listen_stream = {};
#endif

	std::span<const std::byte> probe_path_suffixes = {};
	const char *probe_suffix = nullptr;

	/**
	 * File contents.
	 */
	std::span<const std::byte> read_file = {};

	const char *user = nullptr;

	const char *pool = nullptr;

#if TRANSLATION_ENABLE_HTTP
	HttpStatus status = {};

	bool path_exists = false;
#endif

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
