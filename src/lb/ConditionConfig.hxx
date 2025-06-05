// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "http/Method.hxx"
#include "lib/pcre/UniqueRegex.hxx"
#include "net/MaskedSocketAddress.hxx"

#include <cassert>
#include <string>
#include <utility> // for std::unreachable()
#include <variant>

struct LbAttributeReference {
	enum class Type {
		REMOTE_ADDRESS,
		PEER_SUBJECT,
		PEER_ISSUER_SUBJECT,
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

	template<typename C, typename R>
	[[gnu::pure]]
	const char *GetRequestAttribute(const C &connection, const R &request) const noexcept {
		switch (type) {
		case Type::REMOTE_ADDRESS:
			/* unreachable - handled as a special case */
			break;

		case Type::PEER_SUBJECT:
			return connection.GetPeerSubject();

		case Type::PEER_ISSUER_SUBJECT:
			return connection.GetPeerIssuerSubject();

		case Type::METHOD:
			return http_method_to_string(request.method);

		case Type::URI:
			return request.uri;

		case Type::HEADER:
			return request.headers.Get(name.c_str());
		}

		std::unreachable();
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

	template<typename C, typename R>
	[[gnu::pure]]
	bool MatchRequest(const C &connection, const R &request) const noexcept {
		if (attribute_reference.type == LbAttributeReference::Type::REMOTE_ADDRESS)
			return MatchAddress(request.remote_address);

		const char *s = attribute_reference.GetRequestAttribute(connection, request);
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
			std::unreachable();
		}
	};
};
