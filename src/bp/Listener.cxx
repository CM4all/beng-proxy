/*
 * Copyright 2007-2017 Content Management AG
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
#include "Connection.hxx"
#include "Instance.hxx"
#include "pool/UniquePtr.hxx"
#include "ssl/Factory.hxx"
#include "ssl/Filter.hxx"
#include "ssl/SniCallback.hxx"
#include "fs/FilteredSocket.hxx"
#include "net/SocketAddress.hxx"
#include "io/Logger.hxx"

static std::unique_ptr<SslFactory>
MakeSslFactory(const SslConfig *ssl_config)
{
	if (ssl_config == nullptr)
		return nullptr;

	auto ssl_factory = ssl_factory_new_server(*ssl_config, nullptr);
	// TODO: call SetSessionIdContext()

#ifdef HAVE_NGHTTP2
	ssl_factory->EnableAlpnH2();
#endif

	return ssl_factory;
}

BPListener::BPListener(BpInstance &_instance,
		       TranslationService &_translation_service,
		       const char *_tag,
		       bool _auth_alt_host,
		       const SslConfig *ssl_config)
	:instance(_instance), translation_service(_translation_service),
	 tag(_tag),
	 auth_alt_host(_auth_alt_host),
	 listener(instance.root_pool, instance.event_loop,
		  MakeSslFactory(ssl_config),
		  *this)
{
}

BPListener::~BPListener() noexcept = default;

void
BPListener::OnFilteredSocketConnect(PoolPtr pool,
				    UniquePoolPtr<FilteredSocket> socket,
				    SocketAddress address,
				    const SslFilter *ssl_filter) noexcept
{
	new_connection(std::move(pool), instance, *this,
		       std::move(socket), ssl_filter,
		       address);
}

void
BPListener::OnFilteredSocketError(std::exception_ptr ep) noexcept
{
	LogConcat(2, "listener", ep);
}
