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

#include "Goto.hxx"
#include "Protocol.hxx"
#include "stats/HttpStats.hxx"
#include "io/Logger.hxx"
#include "fs/Listener.hxx"

struct HttpStats;
struct LbListenerConfig;
struct LbInstance;
class LbGotoMap;

/**
 * Listener on a TCP port.
 */
class LbListener final : FilteredSocketListenerHandler {
	LbInstance &instance;

	const LbListenerConfig &config;

	HttpStats http_stats;

	FilteredSocketListener listener;

	LbGoto destination;

	const Logger logger;

	const LbProtocol protocol;

public:
	LbListener(LbInstance &_instance,
		   const LbListenerConfig &_config);

	LbProtocol GetProtocol() const noexcept {
		return protocol;
	}

	const auto &GetConfig() const noexcept {
		return config;
	}

	HttpStats &GetHttpStats() noexcept {
		return http_stats;
	}

	const HttpStats *GetHttpStats() const noexcept {
		return protocol == LbProtocol::HTTP ? &http_stats : nullptr;
	}

	void Scan(LbGotoMap &goto_map);

	unsigned FlushSSLSessionCache(long tm);

private:
	/* virtual methods from class FilteredSocketListenerHandler */
	void OnFilteredSocketConnect(PoolPtr pool,
				     UniquePoolPtr<FilteredSocket> socket,
				     SocketAddress address,
				     const SslFilter *ssl_filter) noexcept override;
	void OnFilteredSocketError(std::exception_ptr e) noexcept override;

};
