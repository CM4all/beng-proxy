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

#include "beng-proxy/Control.hxx"
#include "translation/Protocol.hxx"
#include "net/UniqueSocketDescriptor.hxx"
#include "util/SpanCast.hxx"

#include <span>
#include <string>
#include <string_view>

class BengControlClient {
	UniqueSocketDescriptor socket;

public:
	explicit BengControlClient(UniqueSocketDescriptor _socket) noexcept;
	explicit BengControlClient(const char *host_and_port);

	void AutoBind() const noexcept {
		socket.AutoBind();
	}

	void Send(BengProxy::ControlCommand cmd,
		  std::span<const std::byte> payload={},
		  std::span<const FileDescriptor> fds={}) const;

	void Send(BengProxy::ControlCommand cmd, std::nullptr_t,
		  std::span<const FileDescriptor> fds={}) const {
		Send(cmd, std::span<const std::byte>{}, fds);
	}

	void Send(BengProxy::ControlCommand cmd, std::string_view payload,
		  std::span<const FileDescriptor> fds={}) const {
		Send(cmd, AsBytes(payload), fds);
	}

	std::pair<BengProxy::ControlCommand, std::string> Receive() const;

	static std::string MakeTcacheInvalidate(TranslationCommand cmd,
						std::span<const std::byte> payload) noexcept;

	static std::string MakeTcacheInvalidate(TranslationCommand cmd,
						std::string_view value) noexcept {
		return MakeTcacheInvalidate(cmd, AsBytes(value));
	}
};
