/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "AlpnClient.hxx"
#include "fs/Params.hxx"
#include "fs/Factory.hxx"

#include <string>

class EventLoop;
class SslClientFactory;

class SslSocketFilterFactory final : public SocketFilterFactory {
	EventLoop &event_loop;
	SslClientFactory &ssl_client_factory;
	const std::string host;
	const std::string certificate;
	const SslClientAlpn alpn;

public:
	SslSocketFilterFactory(EventLoop &_event_loop,
			      SslClientFactory &_ssl_client_factory,
			      const char *_host, const char *_certificate,
			      SslClientAlpn _alpn=SslClientAlpn::NONE) noexcept
		:event_loop(_event_loop),
		 ssl_client_factory(_ssl_client_factory),
		 host(_host != nullptr ?  _host : ""),
		 certificate(_certificate != nullptr ? _certificate : ""),
		 alpn(_alpn) {}

	SocketFilterPtr CreateFilter() override;
};

class SslSocketFilterParams final : public SocketFilterParams {
	EventLoop &event_loop;
	SslClientFactory &ssl_client_factory;
	const char *const host;
	const char *const certificate;
	const SslClientAlpn alpn;

public:
	SslSocketFilterParams(EventLoop &_event_loop,
			       SslClientFactory &_ssl_client_factory,
			       const char *_host, const char *_certificate,
			       SslClientAlpn _alpn=SslClientAlpn::NONE) noexcept
		:event_loop(_event_loop),
		 ssl_client_factory(_ssl_client_factory),
		 host(_host),
		 certificate(_certificate), alpn(_alpn) {}

	const char *GetFilterId() const noexcept override {
		return host;
	}

	SocketFilterFactoryPtr CreateFactory() const noexcept override;
};
