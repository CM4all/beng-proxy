/*
 * Copyright 2007-2019 CM4all GmbH
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
#include "ssl/Filter.hxx"
#include "ssl/DbSniCallback.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/SocketAddress.hxx"
#include "util/RuntimeError.hxx"

void
LbListener::OnFilteredSocketConnect(PoolPtr pool,
				    UniquePoolPtr<FilteredSocket> socket,
				    SocketAddress address,
				    const SslFilter *ssl_filter) noexcept
try {
	switch (config.destination.GetProtocol()) {
	case LbProtocol::HTTP:
		NewLbHttpConnection(instance, config, destination,
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
	 logger("listener " + config.name)
{
}

void
LbListener::Setup()
try {
	SslFactory *ssl_factory = nullptr;

	if (config.ssl) {
		/* prepare SSL support */

		std::unique_ptr<SslSniCallback> sni_callback;
		if (config.cert_db != nullptr) {
			auto &cert_cache = instance.GetCertCache(*config.cert_db);
			sni_callback.reset(new DbSslSniCallback(cert_cache));
		}

		ssl_factory = ssl_factory_new_server(config.ssl_config,
						     std::move(sni_callback));

		/* we use the listener name as OpenSSL session_id_context,
		   because listener names are unique, so I hope this should be
		   good enough */
		ssl_factory_set_session_id_context(*ssl_factory, {config.name.data(), config.name.size()});

		if (config.destination.GetProtocol() == LbProtocol::HTTP)
			ssl_factory_enable_alpn_h2(*ssl_factory);
	}

	FilteredSocketListenerHandler &handler = *this;
	listener = std::make_unique<FilteredSocketListener>(instance.root_pool,
							    instance.event_loop,
							    ssl_factory,
							    handler);

	listener->Listen(config.Create(SOCK_STREAM));
} catch (...) {
	std::throw_with_nested(FormatRuntimeError("Failed to set up listener '%s'",
						  config.name.c_str()));
}

void
LbListener::Scan(LbGotoMap &goto_map)
{
	destination = goto_map.GetInstance(config.destination);
}

unsigned
LbListener::FlushSSLSessionCache(long tm)
{
	return listener->FlushSSLSessionCache(tm);
}
