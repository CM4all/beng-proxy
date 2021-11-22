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

#include "lib/pcre/UniqueRegex.hxx"
#include "net/MaskedSocketAddress.hxx"
#include "util/Compiler.h"

#include <cassert>
#include <string>
#include <variant>

struct LbAttributeReference {
	enum class Type {
		REMOTE_ADDRESS,
		METHOD,
		URI,
		HEADER,
	} type;

	std::string name;

	LbAttributeReference(Type _type) noexcept
		:type(_type) {}

	template<typename N>
	LbAttributeReference(Type _type, N &&_name) noexcept
		:type(_type), name(std::forward<N>(_name)) {}

	bool IsAddress() const noexcept {
		return type == Type::REMOTE_ADDRESS;
	}

	template<typename R>
	[[gnu::pure]]
	const char *GetRequestAttribute(const R &request) const noexcept {
		switch (type) {
		case Type::REMOTE_ADDRESS:
			/* unreachable - handled as a special case */
			break;

		case Type::METHOD:
			return http_method_to_string(request.method);

		case Type::URI:
			return request.uri;

		case Type::HEADER:
			return request.headers.Get(name.c_str());
		}

		assert(false);
		gcc_unreachable();
	}

};

struct LbConditionConfig {
	LbAttributeReference attribute_reference;

	bool negate;

	std::variant<std::string, UniqueRegex, MaskedSocketAddress> value;

	LbConditionConfig(LbAttributeReference &&a, bool _negate,
			  const char *_string) noexcept
		:attribute_reference(std::move(a)),
		 negate(_negate), value(_string) {}

	LbConditionConfig(LbAttributeReference &&a, bool _negate,
			  UniqueRegex &&_regex) noexcept
		:attribute_reference(std::move(a)),
		 negate(_negate), value(std::move(_regex)) {}

	LbConditionConfig(LbAttributeReference &&a, bool _negate,
			  MaskedSocketAddress &&_mask) noexcept
		:attribute_reference(std::move(a)),
		 negate(_negate), value(std::move(_mask)) {}

	LbConditionConfig(LbConditionConfig &&other) = default;

	LbConditionConfig(const LbConditionConfig &) = delete;
	LbConditionConfig &operator=(const LbConditionConfig &) = delete;

	[[gnu::pure]]
	bool Match(const char *s) const noexcept {
		return std::visit(MatchHelper{s}, value) ^ negate;
	}

	template<typename R>
	[[gnu::pure]]
	bool MatchRequest(const R &request) const noexcept {
		if (attribute_reference.type == LbAttributeReference::Type::REMOTE_ADDRESS)
			return MatchAddress(request.remote_address);

		const char *s = attribute_reference.GetRequestAttribute(request);
		if (s == nullptr)
			s = "";

		return Match(s);
	}

private:
	[[gnu::pure]]
	bool MatchAddress(SocketAddress address) const noexcept;

	struct MatchHelper {
		const char *s;

		bool operator()(const std::string &v) const noexcept {
			return v == s;
		}

		bool operator()(const UniqueRegex &v) const noexcept {
			return v.Match(s);
		}

		bool operator()(const MaskedSocketAddress &) const noexcept {
			/* unreachable - handled as a special case */
			gcc_unreachable();
		}
	};
};
