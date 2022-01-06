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

#include "Listener.hxx"
#include "Instance.hxx"
#include "ListenerConfig.hxx"
#include "HttpConnection.hxx"
#include "TcpConnection.hxx"
#include "pool/UniquePtr.hxx"
#include "ssl/Factory.hxx"
#include "ssl/DbCertCallback.hxx"
#include "ssl/AlpnProtos.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/SocketAddress.hxx"
#include "lb_features.h"

static std::unique_ptr<SslFactory>
MakeSslFactory(const LbListenerConfig &config,
	       LbInstance &instance)
{
	if (!config.ssl)
		return nullptr;

	/* prepare SSL support */

	std::unique_ptr<SslCertCallback> sni_callback;
#ifdef ENABLE_CERTDB
	if (config.cert_db != nullptr) {
		auto &cert_cache = instance.GetCertCache(*config.cert_db);
		sni_callback.reset(new DbSslCertCallback(cert_cache));
	}
#else
	(void)instance;
#endif

	auto ssl_factory = std::make_unique<SslFactory>(config.ssl_config,
							std::move(sni_callback));

	/* we use the listener name as OpenSSL session_id_context,
	   because listener names are unique, so I hope this should be
	   good enough */
	ssl_factory->SetSessionIdContext({config.name.data(), config.name.size()});

#ifdef HAVE_NGHTTP2
	if (config.GetAlpnHttp2())
		ssl_factory->AddAlpn(alpn_http_any);
#endif

	return ssl_factory;
}

void
LbListener::OnFilteredSocketConnect(PoolPtr pool,
				    UniquePoolPtr<FilteredSocket> socket,
				    SocketAddress address,
				    const SslFilter *ssl_filter) noexcept
try {
	switch (protocol) {
	case LbProtocol::HTTP:
		NewLbHttpConnection(instance, *this, destination,
				    std::move(pool),
				    std::move(socket), ssl_filter,
				    address);
		break;

	case LbProtocol::TCP:
		assert(destination.cluster != nullptr);

		LbTcpConnection::New(instance, config, *destination.cluster,
				     std::move(pool),
				     std::move(socket),
				     address);
		break;
	}
} catch (...) {
	logger(1, "Failed to setup accepted connection: ",
	       std::current_exception());
}

void
LbListener::OnFilteredSocketError(std::exception_ptr ep) noexcept
{
	logger(2, "Failed to accept: ", ep);
}

/*
 * constructor
 *
 */

LbListener::LbListener(LbInstance &_instance,
		       const LbListenerConfig &_config)
	:instance(_instance), config(_config),
	 listener(instance.root_pool, instance.event_loop,
		  MakeSslFactory(config, instance),
		  *this),
	 logger("listener " + config.name),
	 protocol(config.destination.GetProtocol())
{
	listener.Listen(config.Create(SOCK_STREAM));
}

void
LbListener::Scan(LbGotoMap &goto_map)
{
	destination = goto_map.GetInstance(config.destination);
}

unsigned
LbListener::FlushSSLSessionCache(long tm)
{
	return listener.FlushSSLSessionCache(tm);
}
