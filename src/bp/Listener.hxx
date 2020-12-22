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

#pragma once

#include "fs/Listener.hxx"
#include "net/StaticSocketAddress.hxx"

struct BpInstance;
struct SslConfig;
class TranslationService;

/**
 * Listener for incoming HTTP connections.
 */
class BPListener final : FilteredSocketListenerHandler {
	BpInstance &instance;

	TranslationService &translation_service;

	const char *const tag;

	const bool auth_alt_host;

	FilteredSocketListener listener;

public:
	BPListener(BpInstance &_instance,
		   TranslationService &_translation_service,
		   const char *_tag,
		   bool _auth_alt_host,
		   const SslConfig *ssl_config);
	~BPListener() noexcept;

	void Listen(UniqueSocketDescriptor &&_fd) noexcept {
		listener.Listen(std::move(_fd));
	}

	auto GetLocalAddress() const noexcept {
		return listener.GetLocalAddress();
	}

	void AddEvent() noexcept {
		listener.AddEvent();
	}

	void RemoveEvent() noexcept {
		listener.RemoveEvent();
	}

	const char *GetTag() const noexcept {
		return tag;
	}

	bool GetAuthAltHost() const noexcept {
		return auth_alt_host;
	}

	TranslationService &GetTranslationService() const noexcept {
		return translation_service;
	}

private:
	/* virtual methods from class FilteredSocketListenerHandler */
	void OnFilteredSocketConnect(PoolPtr pool,
				     UniquePoolPtr<FilteredSocket> socket,
				     SocketAddress address,
				     const SslFilter *ssl_filter) noexcept override;
	void OnFilteredSocketError(std::exception_ptr e) noexcept override;

};
