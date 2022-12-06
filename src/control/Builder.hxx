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

#include "Padding.hxx"
#include "net/control/Protocol.hxx"
#include "util/ByteOrder.hxx"
#include "util/SpanCast.hxx"

#include <span>
#include <string>

class BengControlBuilder {
	std::string data;

public:
	BengControlBuilder() noexcept {
		static constexpr uint32_t magic = ToBE32(BengProxy::control_magic);
		AppendT(magic);
	}

	void Add(BengProxy::ControlCommand cmd,
		 std::span<const std::byte> payload) noexcept {
		AppendT(BengProxy::ControlHeader{ToBE16(payload.size()), ToBE16(uint16_t(cmd))});
		AppendPadded(payload);
	}

	void Add(BengProxy::ControlCommand cmd,
		 std::string_view payload) noexcept {
		Add(cmd, AsBytes(payload));
	}

	operator std::span<const std::byte>() const noexcept {
		return AsBytes(data);
	}

private:
	void Append(std::string_view s) noexcept {
		data.append(s);
	}

	void Append(std::span<const std::byte> s) noexcept {
		Append(ToStringView(s));
	}

	void AppendPadded(std::span<const std::byte> s) noexcept {
		Append(s);
		data.append(BengProxy::ControlPaddingSize(s.size()), '\0');
	}

	void AppendT(const auto &s) noexcept {
		Append(std::as_bytes(std::span{&s, 1}));
	}
};
