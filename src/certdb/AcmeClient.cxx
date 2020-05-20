/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "AcmeClient.hxx"
#include "AcmeAccount.hxx"
#include "AcmeOrder.hxx"
#include "AcmeAuthorization.hxx"
#include "AcmeChallenge.hxx"
#include "AcmeError.hxx"
#include "AcmeConfig.hxx"
#include "JWS.hxx"
#include "JsonUtil.hxx"
#include "ssl/Base64.hxx"
#include "ssl/Certificate.hxx"
#include "ssl/Key.hxx"
#include "uri/Extract.hxx"
#include "util/Exception.hxx"
#include "util/RuntimeError.hxx"

#include <json/json.h>

#include <memory>

gcc_pure
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

gcc_pure
static Json::Value
ParseJson(GlueHttpResponse &&response)
{
	if (!IsJson(response))
		throw std::runtime_error("JSON expected");

	return ParseJson(std::move(response.body));
}

/**
 * Throw an exception if the given JSON document contains an "error"
 * element.
 */
static void
CheckThrowError(const Json::Value &root)
{
	const auto &error = root["error"];
	if (!error.isNull())
		throw AcmeError(error);
}

/**
 * Throw an exception if the given JSON document contains an "error"
 * element.
 */
static void
CheckThrowError(const Json::Value &root, const char *msg)
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
gcc_noreturn
static void
ThrowError(GlueHttpResponse &&response, const char *msg)
{
	if (IsJson(response)) {
		const auto root = ParseJson(std::move(response.body));
		std::rethrow_exception(NestException(std::make_exception_ptr(AcmeError(root)),
						     std::runtime_error(msg)));
	}

	throw std::runtime_error(msg);
}

/**
 * Throw an exception due to unexpected status.
 */
gcc_noreturn
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

static AcmeDirectory
ParseAcmeDirectory(const Json::Value &json) noexcept
{
	AcmeDirectory directory;
	directory.new_nonce = GetString(json["newNonce"]);
	directory.new_account = GetString(json["newAccount"]);
	directory.new_order = GetString(json["newOrder"]);
	directory.new_authz = GetString(json["new-authz"]);
	directory.new_cert = GetString(json["new-cert"]);
	return directory;
}

void
AcmeClient::RequestDirectory()
{
	if (fake)
		return;

	unsigned remaining_tries = 3;
	while (true) {
		auto response = glue_http_client.Request(event_loop,
							 HTTP_METHOD_GET,
							 (server + "/directory").c_str(),
							 nullptr);
		if (response.status != HTTP_STATUS_OK) {
			if (http_status_is_server_error(response.status) &&
			    --remaining_tries > 0)
				/* try again, just in case it's a
				   temporary Let's Encrypt hiccup */
				continue;

			throw FormatRuntimeError("Unexpected response status %d",
						 response.status);
		}

		if (!IsJson(response))
			throw std::runtime_error("JSON expected");

		directory = ParseAcmeDirectory(ParseJson(std::move(response.body)));
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
							 HTTP_METHOD_HEAD,
							 directory.new_nonce.c_str(),
							 nullptr);
		if (response.status != HTTP_STATUS_OK) {
			if (http_status_is_server_error(response.status) &&
			    --remaining_tries > 0)
				/* try again, just in case it's a temporary Let's
				   Encrypt hiccup */
				continue;

			throw FormatRuntimeError("Unexpected response status %d",
						 response.status);
		}

		if (IsJson(response))
			directory = ParseAcmeDirectory(ParseJson(std::move(response.body)));

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

static Json::Value
MakeHeader(EVP_PKEY &key, const char *url, const char *kid,
	   std::string &&nonce)
{
	Json::Value root;
	root["alg"] = "RS256";
	if (kid != nullptr)
		root["kid"] = kid;
	else
		root["jwk"] = MakeJwk(key);
	root["url"] = url;
	root["nonce"] = std::move(nonce);
	return root;
}

static AllocatedString<>
Sign(EVP_PKEY &key, ConstBuffer<void> data)
{
	UniqueEVP_PKEY_CTX ctx(EVP_PKEY_CTX_new(&key, nullptr));
	if (!ctx)
		throw SslError("EVP_PKEY_CTX_new() failed");

	if (EVP_PKEY_sign_init(ctx.get()) <= 0)
		throw SslError("EVP_PKEY_sign_init() failed");

	if (EVP_PKEY_CTX_set_rsa_padding(ctx.get(), RSA_PKCS1_PADDING) <= 0)
		throw SslError("EVP_PKEY_CTX_set_rsa_padding() failed");

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wcast-qual"

	if (EVP_PKEY_CTX_set_signature_md(ctx.get(), EVP_sha256()) <= 0)
		throw SslError("EVP_PKEY_CTX_set_signature_md() failed");

#pragma GCC diagnostic pop

	unsigned char md[SHA256_DIGEST_LENGTH];
	SHA256((const unsigned char *)data.data, data.size, md);

	size_t length;
	if (EVP_PKEY_sign(ctx.get(), nullptr, &length, md, sizeof(md)) <= 0)
		throw SslError("EVP_PKEY_sign() failed");

	std::unique_ptr<unsigned char[]> buffer(new unsigned char[length]);
	if (EVP_PKEY_sign(ctx.get(), buffer.get(), &length, md, sizeof(md)) <= 0)
		throw SslError("EVP_PKEY_sign() failed");

	return UrlSafeBase64(ConstBuffer<void>(buffer.get(), length));
}

static AllocatedString<>
Sign(EVP_PKEY &key, const char *protected_header_b64, const char *payload_b64)
{
	std::string data(protected_header_b64);
	data += '.';
	data += payload_b64;
	return Sign(key, ConstBuffer<void>(data.data(), data.length()));
}

GlueHttpResponse
AcmeClient::Request(http_method_t method, const char *uri,
		    ConstBuffer<void> body)
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
AcmeClient::Request(http_method_t method, const char *uri,
		    const Json::Value &body)
{
	return Request(method, uri, FormatJson(body));
}

GlueHttpResponse
AcmeClient::SignedRequest(EVP_PKEY &key,
			  http_method_t method, const char *uri,
			  ConstBuffer<void> payload)
{
	const auto payload_b64 = UrlSafeBase64(payload);

	Json::Value root(Json::objectValue);

	const auto protected_header = FormatJson(MakeHeader(key, uri,
							    account_key_id.empty() ? nullptr : account_key_id.c_str(),
							    NextNonce()));

	const auto protected_header_b64 = UrlSafeBase64(protected_header);
	root["payload"] = payload_b64.c_str();

	root["signature"] = Sign(key, protected_header_b64.c_str(),
				 payload_b64.c_str()).c_str();

	root["protected"] = protected_header_b64.c_str();

	return Request(method, uri, root);
}

GlueHttpResponse
AcmeClient::SignedRequest(EVP_PKEY &key,
			  http_method_t method, const char *uri,
			  const Json::Value &payload)
{
	return SignedRequest(key, method, uri, FormatJson(payload).c_str());
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

static Json::Value
MakeMailToString(const char *email) noexcept
{
	return std::string("mailto:") + email;
}

static Json::Value
MakeMailToArray(const char *email) noexcept
{
	Json::Value a(Json::arrayValue);
	a.append(MakeMailToString(email));
	return a;
}

static auto
MakeNewAccountRequest(const char *email, bool only_return_existing) noexcept
{
	Json::Value root(Json::objectValue);
	if (email != nullptr)
		root["contact"] = MakeMailToArray(email);

	if (only_return_existing)
		root["onlyReturnExisting"] = true;

	root["termsOfServiceAgreed"] = true;
	return root;
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
					   HTTP_METHOD_POST,
					   directory.new_account.c_str(),
					   payload);
	if (only_return_existing) {
		if (response.status != HTTP_STATUS_OK)
			ThrowStatusError(std::move(response),
					 "Failed to look up account");
	} else {
		if (response.status == HTTP_STATUS_OK) {
			const auto location = response.headers.find("location");
			if (location != response.headers.end())
				throw FormatRuntimeError("This key is already registered: %s",
							 location->second.c_str());
			else
				throw std::runtime_error("This key is already registered");
		}

		if (response.status != HTTP_STATUS_CREATED)
			ThrowStatusError(std::move(response),
					 "Failed to register account");
	}

	return WithLocation(AcmeAccount{}, response);
}

static Json::Value
DnsIdentifierToJson(const std::string &value) noexcept
{
	Json::Value root(Json::objectValue);
	root["type"] = "dns";
	root["value"] = value;
	return root;
}

static Json::Value
DnsIdentifiersToJson(const std::forward_list<std::string> &identifiers) noexcept
{
	Json::Value root(Json::arrayValue);
	for (const auto &i : identifiers)
		root.append(DnsIdentifierToJson(i));
	return root;
}

static Json::Value
ToJson(const AcmeClient::OrderRequest &request) noexcept
{
	Json::Value root(Json::objectValue);
	root["identifiers"] = DnsIdentifiersToJson(request.identifiers);
	return root;
}

static auto
ToOrder(const Json::Value &root)
{
	if (!root.isObject())
		throw std::runtime_error("Response is not an object");

	const auto &status = root["status"];
	if (!status.isString())
		throw std::runtime_error("No status");

	const auto &authorizations = root["authorizations"];
	if (!authorizations.isArray() || authorizations.size() == 0)
		throw std::runtime_error("No authorizations");

	const auto &finalize = root["finalize"];
	if (!finalize.isString())
		throw std::runtime_error("No finalize URL");

	AcmeOrder order;

	order.status = status.asString();

	for (const auto &i : authorizations) {
		if (!i.isString())
			throw std::runtime_error("Authorization is not a string");

		order.authorizations.emplace_front(i.asString());
	}

	order.finalize = finalize.asString();

	const auto &certificate = root["certificate"];
	if (certificate.isString())
		order.certificate = certificate.asString();

	return order;
}

AcmeOrder
AcmeClient::NewOrder(EVP_PKEY &key, OrderRequest &&request)
{
	EnsureDirectory();
	if (directory.new_order.empty())
		throw std::runtime_error("No newOrder in directory");

	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST,
					   directory.new_order.c_str(),
					   ToJson(request));
	if (response.status != HTTP_STATUS_CREATED)
		ThrowStatusError(std::move(response),
				 "Failed to create order");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to create order");
	return WithLocation(ToOrder(root), response);
}

static Json::Value
ToJson(X509_REQ &req) noexcept
{
	Json::Value root(Json::objectValue);
	root["csr"] = UrlSafeBase64(req).c_str();
	return root;
}

AcmeOrder
AcmeClient::FinalizeOrder(EVP_PKEY &key, const AcmeOrder &order,
			  X509_REQ &csr)
{
	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST,
					   order.finalize.c_str(),
					   ToJson(csr));
	if (response.status != HTTP_STATUS_OK)
		ThrowStatusError(std::move(response),
				 "Failed to finalize order");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to finalize order");
	return WithLocation(ToOrder(root), response);
}

UniqueX509
AcmeClient::DownloadCertificate(EVP_PKEY &key, const AcmeOrder &order)
{
	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST,
					   order.certificate.c_str(),
					   "");
	if (response.status != HTTP_STATUS_OK)
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

static auto
ToChallenge(const Json::Value &root)
{
	if (!root.isObject())
		throw std::runtime_error("Challenge is not an object");

	const auto &type = root["type"];
	if (!type.isString())
		throw std::runtime_error("No type");

	const auto &url = root["url"];
	if (!url.isString())
		throw std::runtime_error("No url");

	const auto &status = root["status"];
	if (!status.isString())
		throw std::runtime_error("No status");

	const auto &token = root["token"];
	if (!token.isString())
		throw std::runtime_error("No token");

	AcmeChallenge challenge;
	challenge.type = type.asString();
	challenge.uri = url.asString();
	challenge.status = AcmeChallenge::ParseStatus(status.asString());
	challenge.token = token.asString();

	try {
		CheckThrowError(root);
	} catch (...) {
		challenge.error = std::current_exception();
	}

	return challenge;
}

static auto
ToAuthorization(const Json::Value &root)
{
	if (!root.isObject())
		throw std::runtime_error("Response is not an object");

	const auto &status = root["status"];
	if (!status.isString())
		throw std::runtime_error("No status");

	const auto &identifier = root["identifier"];
	if (!identifier.isObject())
		throw std::runtime_error("No identifier");

	const auto &iv = identifier["value"];
	if (!iv.isString())
		throw std::runtime_error("No value");

	const auto &challenges = root["challenges"];
	if (!challenges.isArray() || challenges.size() == 0)
		throw std::runtime_error("No challenges");

	AcmeAuthorization response;
	response.status = AcmeAuthorization::ParseStatus(status.asString());
	response.identifier = iv.asString();

	for (const auto &i : challenges)
		response.challenges.emplace_front(ToChallenge(i));

	const auto &wildcard = root["wildcard"];
	response.wildcard = wildcard.isBool() && wildcard.asBool();

	return response;
}

AcmeAuthorization
AcmeClient::Authorize(EVP_PKEY &key, const char *url)
{
	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST,
					   url,
					   Json::Value(Json::stringValue));
	if (response.status != HTTP_STATUS_OK)
		ThrowStatusError(std::move(response),
				 "Failed to request authorization");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to request authorization");
	return ToAuthorization(root);
}

AcmeAuthorization
AcmeClient::PollAuthorization(EVP_PKEY &key, const char *url)
{
	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST,
					   url,
					   "");
	if (response.status != HTTP_STATUS_OK)
		ThrowStatusError(std::move(response),
				 "Failed to poll authorization");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to poll authorization");
	return ToAuthorization(root);
}

AcmeChallenge
AcmeClient::UpdateChallenge(EVP_PKEY &key, const AcmeChallenge &challenge)
{
	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST,
					   challenge.uri.c_str(),
					   Json::Value(Json::objectValue));
	if (response.status != HTTP_STATUS_OK)
		ThrowStatusError(std::move(response),
				 "Failed to update challenge");

	auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to update challenge");
	return ToChallenge(root);
}
