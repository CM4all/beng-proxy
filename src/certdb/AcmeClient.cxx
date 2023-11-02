// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AcmeClient.hxx"
#include "AcmeAccount.hxx"
#include "AcmeOrder.hxx"
#include "AcmeAuthorization.hxx"
#include "AcmeChallenge.hxx"
#include "AcmeError.hxx"
#include "AcmeConfig.hxx"
#include "JWS.hxx"
#include "jwt/RS256.hxx"
#include "http/Method.hxx"
#include "http/Status.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/Buffer.hxx"
#include "lib/openssl/UniqueBIO.hxx"
#include "lib/sodium/Base64.hxx"
#include "util/AllocatedString.hxx"
#include "util/Exception.hxx"

#include <nlohmann/json.hpp>

#include <memory>

using std::string_view_literals::operator""sv;
using json = nlohmann::json;

[[gnu::pure]]
static bool
IsJson(const GlueHttpResponse &response) noexcept
{
	auto i = response.headers.find("content-type");
	if (i == response.headers.end())
		return false;

	const char *content_type = i->second.c_str();
	return strcmp(content_type, "application/json") == 0 ||
		strcmp(content_type, "application/jose+json") == 0 ||
		strcmp(content_type, "application/problem+json") == 0;
}

[[gnu::pure]]
static json
ParseJson(GlueHttpResponse &&response)
{
	if (!IsJson(response))
		throw std::runtime_error("JSON expected");

	return json::parse(response.body);
}

/**
 * Throw an exception if the given JSON document contains an "error"
 * element.
 */
static void
CheckThrowError(const json &root)
{
	const auto error = root.find("error"sv);
	if (error == root.end())
		return;

	if (error->is_object())
		throw AcmeError(*error);
}

/**
 * Throw an exception if the given JSON document contains an "error"
 * element.
 */
static void
CheckThrowError(const json &root, const char *msg)
{
	try {
		CheckThrowError(root);
	} catch (...) {
		std::throw_with_nested(std::runtime_error(msg));
	}
}

/**
 * Throw an exception, adding "detail" from the JSON document (if the
 * response is JSON).
 */
[[noreturn]]
static void
ThrowError(GlueHttpResponse &&response, const char *msg)
{
	if (IsJson(response)) {
		const auto root = json::parse(response.body);
		std::rethrow_exception(NestException(std::make_exception_ptr(AcmeError(root)),
						     std::runtime_error(msg)));
	}

	throw std::runtime_error(msg);
}

/**
 * Throw an exception due to unexpected status.
 */
[[noreturn]]
static void
ThrowStatusError(GlueHttpResponse &&response, const char *msg)
{
	std::string what(msg);
	what += " (";
	what += http_status_to_string(response.status);
	what += ")";

	ThrowError(std::move(response), what.c_str());
}

AcmeClient::AcmeClient(const AcmeConfig &config) noexcept
	:glue_http_client(event_loop),
	 server(config.staging
		? "https://acme-staging-v02.api.letsencrypt.org"
		: "https://acme-v02.api.letsencrypt.org"),
	 account_key_id(config.account_key_id),
	 fake(config.fake)
{
	if (config.debug)
		glue_http_client.EnableVerbose();
}

AcmeClient::~AcmeClient() noexcept = default;

static void
from_json(const json &j, AcmeDirectory &directory)
{
	j.at("newNonce"sv).get_to(directory.new_nonce);
	j.at("newAccount"sv).get_to(directory.new_account);
	j.at("newOrder"sv).get_to(directory.new_order);
}

void
AcmeClient::RequestDirectory()
{
	if (fake)
		return;

	unsigned remaining_tries = 3;
	while (true) {
		auto response = glue_http_client.Request(event_loop,
							 HttpMethod::GET,
							 (server + "/directory").c_str(),
							 {});
		if (response.status != HttpStatus::OK) {
			if (http_status_is_server_error(response.status) &&
			    --remaining_tries > 0)
				/* try again, just in case it's a
				   temporary Let's Encrypt hiccup */
				continue;

			throw FmtRuntimeError("Unexpected response status {}",
					      (unsigned)response.status);
		}

		if (!IsJson(response))
			throw std::runtime_error("JSON expected");

		json::parse(response.body).get_to(directory);
		break;
	}
}

void
AcmeClient::EnsureDirectory()
{
	if (fake)
		return;

	if (directory.new_nonce.empty())
		RequestDirectory();
}

std::string
AcmeClient::RequestNonce()
{
	if (fake)
		return "foo";

	EnsureDirectory();
	if (directory.new_nonce.empty())
		throw std::runtime_error("No newNonce in directory");

	unsigned remaining_tries = 3;
	while (true) {
		auto response = glue_http_client.Request(event_loop,
							 HttpMethod::HEAD,
							 directory.new_nonce.c_str(),
							 {});
		if (response.status != HttpStatus::OK) {
			if (http_status_is_server_error(response.status) &&
			    --remaining_tries > 0)
				/* try again, just in case it's a temporary Let's
				   Encrypt hiccup */
				continue;

			throw FmtRuntimeError("Unexpected response status {}",
					      (unsigned)response.status);
		}

		if (IsJson(response))
			json::parse(response.body).get_to(directory);

		auto nonce = response.headers.find("replay-nonce");
		if (nonce == response.headers.end())
			throw std::runtime_error("No Replay-Nonce response header");
		return nonce->second.c_str();
	}
}

std::string
AcmeClient::NextNonce()
{
	if (next_nonce.empty())
		next_nonce = RequestNonce();

	std::string result;
	std::swap(result, next_nonce);
	return result;
}

static json
MakeHeader(EVP_PKEY &key, const char *url, const char *kid,
	   std::string_view nonce)
{
	json root{
		{"alg", "RS256"},
		{"url", url},
		{"nonce", nonce},
	};
	if (kid != nullptr)
		root.emplace("kid", kid);
	else
		root.emplace("jwk", MakeJwk(key));
	return root;
}

GlueHttpResponse
AcmeClient::Request(HttpMethod method, const char *uri,
		    std::span<const std::byte> body)
{
	auto response = fake
		? FakeRequest(method, uri, body)
		: glue_http_client.Request(event_loop,
					   method, uri,
					   body);

	auto new_nonce = response.headers.find("replay-nonce");
	if (new_nonce != response.headers.end())
		next_nonce = std::move(new_nonce->second);

	return response;
}

GlueHttpResponse
AcmeClient::Request(HttpMethod method, const char *uri,
		    const json &body)
{
	return Request(method, uri, body.dump());
}

GlueHttpResponse
AcmeClient::SignedRequest(EVP_PKEY &key,
			  HttpMethod method, const char *uri,
			  std::span<const std::byte> payload)
{
	const auto payload_b64 = UrlSafeBase64(payload);

	const auto protected_header =
		MakeHeader(key, uri,
			   account_key_id.empty()
			   ? nullptr
			   : account_key_id.c_str(),
			   NextNonce()).dump();

	const auto protected_header_b64 = UrlSafeBase64(protected_header);

	const json root{
		{"payload", payload_b64.c_str()},
		{"signature",
		 JWT::SignRS256(key, protected_header_b64.c_str(),
				payload_b64.c_str()).c_str()},
		{"protected", protected_header_b64.c_str()},
	};

	return Request(method, uri, root);
}

GlueHttpResponse
AcmeClient::SignedRequest(EVP_PKEY &key,
			  HttpMethod method, const char *uri,
			  const json &payload)
{
	return SignedRequest(key, method, uri,
			     payload.dump());
}

template<typename T>
static auto
WithLocation(T &&t, const GlueHttpResponse &response) noexcept
{
	auto location = response.headers.find("location");
	if (location != response.headers.end())
		t.location = std::move(location->second);

	return std::move(t);
}

static json
MakeMailToString(const char *email) noexcept
{
	return fmt::format("mailto:{}"sv, email);
}

static json
MakeMailToArray(const char *email) noexcept
{
	return {MakeMailToString(email)};
}

static auto
MakeNewAccountRequest(const char *email, bool only_return_existing) noexcept
{
	json root{
		{"termsOfServiceAgreed", true},
	};

	if (email != nullptr)
		root.emplace("contact", MakeMailToArray(email));

	if (only_return_existing)
		root.emplace("onlyReturnExisting", true);

	return root;
}

static void
from_json(const json &j, AcmeAccount::Status &status)
{
	status = AcmeAccount::ParseStatus(j.get<std::string_view>());
}

static void
from_json(const json &j, AcmeAccount &account)
{
	j.at("status"sv).get_to(account.status);

	if (const auto contact = j.find("contact"sv); contact != j.end())
		contact->get_to(account.contact);
}

AcmeAccount
AcmeClient::NewAccount(EVP_PKEY &key, const char *email,
		       bool only_return_existing)
{
	EnsureDirectory();
	if (directory.new_account.empty())
		throw std::runtime_error("No newAccount in directory");

	const auto payload = MakeNewAccountRequest(email, only_return_existing);

	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   directory.new_account.c_str(),
					   payload);
	if (only_return_existing) {
		if (response.status != HttpStatus::OK)
			ThrowStatusError(std::move(response),
					 "Failed to look up account");
	} else {
		if (response.status == HttpStatus::OK) {
			const auto location = response.headers.find("location");
			if (location != response.headers.end())
				throw FmtRuntimeError("This key is already registered: {}",
						      location->second);
			else
				throw std::runtime_error("This key is already registered");
		}

		if (response.status != HttpStatus::CREATED)
			ThrowStatusError(std::move(response),
					 "Failed to register account");
	}

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to create account");
	return WithLocation(root.get<AcmeAccount>(), response);
}

static json
DnsIdentifierToJson(std::string_view value) noexcept
{
	return {
		{"type", "dns"},
		{"value", value},
	};
}

static json
DnsIdentifiersToJson(const std::forward_list<std::string> &identifiers) noexcept
{
	json root = json::value_t::array;
	for (const auto &i : identifiers)
		root.emplace_back(DnsIdentifierToJson(i));
	return root;
}

static void
to_json(json &jv, const AcmeClient::OrderRequest &request) noexcept
{
	jv = {
		{"identifiers", DnsIdentifiersToJson(request.identifiers)},
	};
}

static void
from_json(const json &j, AcmeOrder &order)
{
	j.at("status"sv).get_to(order.status);

	if (auto authorizations = j.find("authorizations"sv); authorizations != j.end())
		authorizations->get_to(order.authorizations);

	j.at("finalize"sv).get_to(order.finalize);

	if (auto certificate = j.find("certificate"sv); certificate != j.end())
		certificate->get_to(order.certificate);
}

AcmeOrder
AcmeClient::NewOrder(EVP_PKEY &key, OrderRequest &&request)
{
	EnsureDirectory();
	if (directory.new_order.empty())
		throw std::runtime_error("No newOrder in directory");

	const json request_json = request;
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   directory.new_order.c_str(),
					   request_json);
	if (response.status != HttpStatus::CREATED)
		ThrowStatusError(std::move(response),
				 "Failed to create order");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to create order");
	return WithLocation(root.get<AcmeOrder>(), response);
}

static json
ToJson(X509_REQ &req) noexcept
{
	return {
		{"csr", UrlSafeBase64(SslBuffer(req).get()).c_str()},
	};
}

AcmeOrder
AcmeClient::FinalizeOrder(EVP_PKEY &key, const AcmeOrder &order,
			  X509_REQ &csr)
{
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   order.finalize.c_str(),
					   ToJson(csr));
	if (response.status != HttpStatus::OK)
		ThrowStatusError(std::move(response),
				 "Failed to finalize order");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to finalize order");
	return WithLocation(root.get<AcmeOrder>(), response);
}

UniqueX509
AcmeClient::DownloadCertificate(EVP_PKEY &key, const AcmeOrder &order)
{
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   order.certificate.c_str(),
					   ""sv);
	if (response.status != HttpStatus::OK)
		ThrowStatusError(std::move(response),
				 "Failed to download certificate");

	auto ct = response.headers.find("content-type");
	if (ct == response.headers.end() ||
	    ct->second != "application/pem-certificate-chain")
		throw std::runtime_error("Wrong Content-Type in certificate download");

	UniqueBIO in(BIO_new_mem_buf(response.body.data(), response.body.length()));
	return UniqueX509((X509 *)PEM_ASN1_read_bio((d2i_of_void *)d2i_X509,
						    PEM_STRING_X509, in.get(),
						    nullptr, nullptr, nullptr));
}

static void
from_json(const json &j, AcmeChallenge::Status &status)
{
	status = AcmeChallenge::ParseStatus(j.get<std::string_view>());
}

static void
from_json(const json &j, AcmeChallenge &challenge)
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

static void
from_json(const json &j, AcmeAuthorization::Status &status)
{
	status = AcmeAuthorization::ParseStatus(j.get<std::string_view>());
}

static void
from_json(const json &j, AcmeAuthorization &response)
{
	j.at("status"sv).get_to(response.status);
	j.at("identifier"sv).at("value"sv).get_to(response.identifier);
	j.at("challenges"sv).get_to(response.challenges);
	if (response.challenges.empty())
		throw std::runtime_error("No challenges");

	if (auto wildcard = j.find("wildcard"sv); wildcard != j.end())
		wildcard->get_to(response.wildcard);
}

AcmeAuthorization
AcmeClient::Authorize(EVP_PKEY &key, const char *url)
{
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   url,
					   ""sv);
	if (response.status != HttpStatus::OK)
		ThrowStatusError(std::move(response),
				 "Failed to request authorization");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to request authorization");
	return root.get<AcmeAuthorization>();
}

AcmeAuthorization
AcmeClient::PollAuthorization(EVP_PKEY &key, const char *url)
{
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   url,
					   ""sv);
	if (response.status != HttpStatus::OK)
		ThrowStatusError(std::move(response),
				 "Failed to poll authorization");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to poll authorization");
	return root.get<AcmeAuthorization>();
}

AcmeChallenge
AcmeClient::UpdateChallenge(EVP_PKEY &key, const AcmeChallenge &challenge)
{
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   challenge.uri.c_str(),
					   json::value_t::object);
	if (response.status != HttpStatus::OK)
		ThrowStatusError(std::move(response),
				 "Failed to update challenge");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to update challenge");
	return root.get<AcmeChallenge>();
}
