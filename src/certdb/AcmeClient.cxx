// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AcmeClient.hxx"
#include "AcmeJson.hxx"
#include "AcmeAccount.hxx"
#include "AcmeOrder.hxx"
#include "AcmeAuthorization.hxx"
#include "AcmeChallenge.hxx"
#include "AcmeError.hxx"
#include "AcmeConfig.hxx"
#include "jwt/OsslJWK.hxx"
#include "jwt/OsslJWS.hxx"
#include "http/Method.hxx"
#include "http/Status.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/Buffer.hxx"
#include "lib/openssl/UniqueBIO.hxx"
#include "lib/sodium/Base64.hxx"
#include "util/AllocatedString.hxx"
#include "util/Exception.hxx"
#include "util/MimeType.hxx"
#include "util/SpanCast.hxx"

#include <fmt/core.h>
#include <nlohmann/json.hpp>

#include <memory>

using std::string_view_literals::operator""sv;
using json = nlohmann::json;

[[gnu::pure]]
static bool
IsJson(const StringCurlResponse &response) noexcept
{
	auto i = response.headers.find("content-type");
	if (i == response.headers.end())
		return false;

	const std::string_view content_type = GetMimeTypeBase(i->second);
	return content_type == "application/json"sv ||
		content_type ==  "application/jose+json" ||
		content_type == "application/problem+json"sv;
}

[[gnu::pure]]
static json
ParseJson(StringCurlResponse &&response)
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
ThrowError(StringCurlResponse &&response, const char *msg)
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
ThrowStatusError(StringCurlResponse &&response, const char *msg)
{
	ThrowError(std::move(response),
		   fmt::format("{} ({})"sv, msg, http_status_to_string(response.status)).c_str());
}

AcmeClient::AcmeClient(const AcmeConfig &config)
	:glue_http_client(config.tls_ca.empty() ? nullptr : config.tls_ca.c_str()),
	 directory_url(config.GetDirectoryURL()),
	 account_key_id(config.account_key_id)
{
	if (config.debug)
		glue_http_client.EnableVerbose();
}

AcmeClient::~AcmeClient() noexcept = default;

void
AcmeClient::RequestDirectory()
{
	unsigned remaining_tries = 3;
	while (true) {
		auto response = glue_http_client.Request(HttpMethod::GET,
							 directory_url,
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
	if (directory.new_nonce.empty())
		RequestDirectory();
}

std::string
AcmeClient::RequestNonce()
{
	EnsureDirectory();
	if (directory.new_nonce.empty())
		throw std::runtime_error("No newNonce in directory");

	unsigned remaining_tries = 3;
	while (true) {
		auto response = glue_http_client.Request(HttpMethod::HEAD,
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
MakeHeader(const EVP_PKEY &key, const char *url, const char *kid,
	   std::string_view nonce)
{
	json root{
		{"alg", JWS::GetAlg(key)},
		{"url", url},
		{"nonce", nonce},
	};
	if (kid != nullptr)
		root.emplace("kid", kid);
	else
		root.emplace("jwk", ToJWK(key));
	return root;
}

StringCurlResponse
AcmeClient::Request(HttpMethod method, const char *uri,
		    std::span<const std::byte> body)
{
	auto response = glue_http_client.Request(method, uri,
						 body);

	if (auto new_nonce = response.headers.find("replay-nonce");
	    new_nonce != response.headers.end()) {
		next_nonce = std::move(new_nonce->second);
		response.headers.erase(new_nonce);
	}

	return response;
}

StringCurlResponse
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
		 JWS::Sign(key, protected_header_b64.c_str(),
			   payload_b64.c_str()).c_str()},
		{"protected", protected_header_b64.c_str()},
	};

	return Request(method, uri, AsBytes(root.dump()));
}

StringCurlResponse
AcmeClient::SignedRequestRetry(EVP_PKEY &key,
			       HttpMethod method, const char *uri,
			       std::span<const std::byte> payload)
{
	constexpr unsigned max_attempts = 3;
	for (unsigned remaining_attempts = max_attempts;;) {
		auto response = SignedRequest(key, method, uri, payload);

		if (response.status == HttpStatus::BAD_REQUEST && IsJson(response)) {
			try {
				throw AcmeError{ParseJson(std::move(response))};
			} catch (const AcmeError &e) {
				if (e.GetType() == "urn:ietf:params:acme:error:badNonce"sv)
					/* try again */
					continue;

				throw;
			}
		}

		if (!http_status_is_server_error(response.status) ||
		    --remaining_attempts == 0)
			return response;
	}
}

template<typename T>
static auto
WithLocation(T &&t, const StringCurlResponse &response) noexcept
{
	auto location = response.headers.find("location");
	if (location != response.headers.end())
		t.location = std::move(location->second);

	return std::move(t);
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
					   AsBytes(payload.dump()));
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

AcmeOrder
AcmeClient::NewOrder(EVP_PKEY &key, AcmeOrderRequest &&request)
{
	EnsureDirectory();
	if (directory.new_order.empty())
		throw std::runtime_error("No newOrder in directory");

	const json request_json = request;
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   directory.new_order.c_str(),
					   AsBytes(request_json.dump()));
	if (response.status != HttpStatus::CREATED)
		ThrowStatusError(std::move(response),
				 "Failed to create order");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to create order");
	return WithLocation(root.get<AcmeOrder>(), response);
}

static json
ToJson(const X509_REQ &req) noexcept
{
	return {
		{"csr", UrlSafeBase64(SslBuffer(req).get()).c_str()},
	};
}

AcmeOrder
AcmeClient::FinalizeOrder(EVP_PKEY &key, const AcmeOrder &order,
			  const X509_REQ &csr)
{
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   order.finalize.c_str(),
					   AsBytes(ToJson(csr).dump()));
	if (response.status != HttpStatus::OK)
		ThrowStatusError(std::move(response),
				 "Failed to finalize order");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to finalize order");
	return WithLocation(root.get<AcmeOrder>(), response);
}

AcmeOrder
AcmeClient::PollOrder(EVP_PKEY &key, const char *url)
{
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   url,
					   {});
	if (response.status != HttpStatus::OK)
		ThrowStatusError(std::move(response),
				 "Failed to poll order");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to poll order");
	return root.get<AcmeOrder>();
}

UniqueX509
AcmeClient::DownloadCertificate(EVP_PKEY &key, const AcmeOrder &order)
{
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   order.certificate.c_str(),
					   {});
	if (response.status != HttpStatus::OK)
		ThrowStatusError(std::move(response),
				 "Failed to download certificate");

	auto ct = response.headers.find("content-type");
	if (ct == response.headers.end() ||
	    GetMimeTypeBase(ct->second) != "application/pem-certificate-chain"sv)
		throw std::runtime_error("Wrong Content-Type in certificate download");

	UniqueBIO in(BIO_new_mem_buf(response.body.data(), response.body.length()));
	return UniqueX509((X509 *)PEM_ASN1_read_bio((d2i_of_void *)d2i_X509,
						    PEM_STRING_X509, in.get(),
						    nullptr, nullptr, nullptr));
}

AcmeAuthorization
AcmeClient::Authorize(EVP_PKEY &key, const char *url)
{
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   url,
					   {});
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
					   {});
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
	const json request_body = json::value_t::object;
	auto response = SignedRequestRetry(key,
					   HttpMethod::POST,
					   challenge.uri.c_str(),
					   AsBytes(request_body.dump()));
	if (response.status != HttpStatus::OK)
		ThrowStatusError(std::move(response),
				 "Failed to update challenge");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to update challenge");
	return root.get<AcmeChallenge>();
}
