// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "AlpnClient.hxx"
#include "fs/Factory.hxx"

class EventLoop;
class SslClientFactory;

class SslSocketFilterFactory final : public SocketFilterFactory {
	EventLoop &event_loop;
	SslClientFactory &ssl_client_factory;
	const char *const host;
	const char *const certificate;
	const SslClientAlpn alpn;

public:
	SslSocketFilterFactory(EventLoop &_event_loop,
			       SslClientFactory &_ssl_client_factory,
			       const char *_host, const char *_certificate,
			       SslClientAlpn _alpn=SslClientAlpn::NONE)
		:event_loop(_event_loop),
		 ssl_client_factory(_ssl_client_factory),
		 host(_host),
		 certificate(_certificate), alpn(_alpn) {}

	const char *GetFilterId() const override {
		return host;
	}

	SocketFilterPtr CreateFilter() override;
};
