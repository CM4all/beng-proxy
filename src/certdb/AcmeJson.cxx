// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AcmeJson.hxx"
#include "AcmeError.hxx"
#include "AcmeDirectory.hxx"
#include "AcmeAccount.hxx"
#include "AcmeOrder.hxx"
#include "AcmeAuthorization.hxx"
#include "AcmeChallenge.hxx"

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <stdexcept>

using std::string_view_literals::operator""sv;

void
CheckThrowError(const nlohmann::json &root)
{
	const auto error = root.find("error"sv);
	if (error != root.end() && error->is_object())
		throw AcmeError{*error};
}

void
from_json(const nlohmann::json &j, AcmeDirectory &directory)
{
	j.at("newNonce"sv).get_to(directory.new_nonce);
	j.at("newAccount"sv).get_to(directory.new_account);
	j.at("newOrder"sv).get_to(directory.new_order);
}

static nlohmann::json
MakeMailToString(const char *email) noexcept
{
	return fmt::format("mailto:{}"sv, email);
}

static nlohmann::json
MakeMailToArray(const char *email) noexcept
{
	return {MakeMailToString(email)};
}

nlohmann::json
MakeNewAccountRequest(const char *email, bool only_return_existing) noexcept
{
	nlohmann::json root{
		{"termsOfServiceAgreed", true},
	};

	if (email != nullptr)
		root.emplace("contact", MakeMailToArray(email));

	if (only_return_existing)
		root.emplace("onlyReturnExisting", true);

	return root;
}

inline void
from_json(const nlohmann::json &j, AcmeAccount::Status &status)
{
	status = AcmeAccount::ParseStatus(j.get<std::string_view>());
}

void
from_json(const nlohmann::json &j, AcmeAccount &account)
{
	j.at("status"sv).get_to(account.status);

	if (const auto contact = j.find("contact"sv); contact != j.end())
		contact->get_to(account.contact);
}

static nlohmann::json
DnsIdentifierToJson(std::string_view value) noexcept
{
	return {
		{"type", "dns"},
		{"value", value},
	};
}

static nlohmann::json
DnsIdentifiersToJson(const std::forward_list<std::string> &identifiers) noexcept
{
	nlohmann::json root = nlohmann::json::value_t::array;
	for (const auto &i : identifiers)
		root.emplace_back(DnsIdentifierToJson(i));
	return root;
}

void
to_json(nlohmann::json &jv, const AcmeOrderRequest &request) noexcept
{
	jv = {
		{"identifiers", DnsIdentifiersToJson(request.identifiers)},
	};
}

static void
from_json(const nlohmann::json &j, AcmeOrder::Status &status)
{
	status = AcmeOrder::ParseStatus(j.get<std::string_view>());
}

void
from_json(const nlohmann::json &j, AcmeOrder &order)
{
	j.at("status"sv).get_to(order.status);

	if (auto authorizations = j.find("authorizations"sv); authorizations != j.end())
		authorizations->get_to(order.authorizations);

	j.at("finalize"sv).get_to(order.finalize);

	if (auto certificate = j.find("certificate"sv); certificate != j.end())
		certificate->get_to(order.certificate);
}

inline void
from_json(const nlohmann::json &j, AcmeChallenge::Status &status)
{
	status = AcmeChallenge::ParseStatus(j.get<std::string_view>());
}

void
from_json(const nlohmann::json &j, AcmeChallenge &challenge)
{
	j.at("type"sv).get_to(challenge.type);
	j.at("url"sv).get_to(challenge.uri);
	j.at("status"sv).get_to(challenge.status);
	j.at("token"sv).get_to(challenge.token);

	try {
		CheckThrowError(j);
	} catch (...) {
		challenge.error = std::current_exception();
	}
}

inline void
from_json(const nlohmann::json &j, AcmeAuthorization::Status &status)
{
	status = AcmeAuthorization::ParseStatus(j.get<std::string_view>());
}

void
from_json(const nlohmann::json &j, AcmeAuthorization &response)
{
	j.at("status"sv).get_to(response.status);
	j.at("identifier"sv).at("value"sv).get_to(response.identifier);
	j.at("challenges"sv).get_to(response.challenges);
	if (response.challenges.empty())
		throw std::runtime_error("No challenges");

	if (auto wildcard = j.find("wildcard"sv); wildcard != j.end())
		wildcard->get_to(response.wildcard);
}
