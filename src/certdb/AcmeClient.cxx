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
CheckThrowError(const Json::Value &root, const char *msg)
{
	const auto &error = root["error"];
	if (error.isNull())
		return;

	std::rethrow_exception(NestException(std::make_exception_ptr(AcmeError(error)),
					     std::runtime_error(msg)));
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
		? "https://acme-staging.api.letsencrypt.org"
		: "https://acme-v01.api.letsencrypt.org"),
	 agreement_url(config.agreement_url),
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
	directory.new_reg = GetString(json["new-reg"]);
	directory.new_authz = GetString(json["new-authz"]);
	directory.new_cert = GetString(json["new-cert"]);
	return directory;
}

std::string
AcmeClient::RequestNonce()
{
	if (fake)
		return "foo";

	unsigned remaining_tries = 3;
	while (true) {
		auto response = glue_http_client.Request(event_loop,
							 HTTP_METHOD_GET,
							 (server + "/directory").c_str(),
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

void
AcmeClient::EnsureDirectory()
{
	if (next_nonce.empty())
		next_nonce = RequestNonce();
}

static std::string
MakeHeader(EVP_PKEY &key) noexcept
{
	auto jwk = MakeJwk(key);

	std::string header("{\"alg\": \"RS256\", \"jwk\": ");
	header += jwk;
	header += "}";
	return header;
}

static std::string
WithNonce(const std::string &_header, const std::string &nonce) noexcept
{
	std::string header(_header);
	assert(header.size() > 8);

	size_t i = header.length() - 1;
	std::string s(", \"nonce\": \"");
	s += nonce;
	s += "\"";

	header.insert(i, s);
	return header;
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
AcmeClient::SignedRequest(EVP_PKEY &key,
			  http_method_t method, const char *uri,
			  ConstBuffer<void> payload)
{
	const auto payload_b64 = UrlSafeBase64(payload);

	const auto header = MakeHeader(key);

	const auto nonce = NextNonce();

	const auto protected_header = WithNonce(header, nonce);

	const auto protected_header_b64 = UrlSafeBase64(protected_header);

	const auto signature = Sign(key, protected_header_b64.c_str(),
				    payload_b64.c_str());

	std::string body = "{\"signature\": \"";
	body += signature.c_str();
	body += "\", \"payload\": \"";
	body += payload_b64.c_str();
	body += "\", \"header\": ";
	body += header;
	body += ", \"protected\": \"";
	body += protected_header_b64.c_str();
	body += "\"}";

	return Request(method, uri,
		       {body.data(), body.length()});
}

GlueHttpResponse
AcmeClient::SignedRequest(EVP_PKEY &key,
			  http_method_t method, const char *uri,
			  const Json::Value &payload)
{
	return SignedRequest(key, method, uri, FormatJson(payload).c_str());
}

AcmeClient::Account
AcmeClient::NewReg(EVP_PKEY &key, const char *email)
{
	EnsureDirectory();
	if (directory.new_reg.empty())
		throw std::runtime_error("No new-reg in directory");

	std::string payload("{\"resource\": \"new-reg\", ");

	if (email != nullptr) {
		payload += "\"contact\": [ \"mailto:";
		payload += email;
		payload += "\" ], ";
	}

	payload += "\"agreement\": \"";
	payload += agreement_url;
	payload += "\"}";

	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST,
					   directory.new_reg.c_str(),
					   payload.c_str());
	if (response.status == HTTP_STATUS_OK)
		throw std::runtime_error("This key is already registered");

	if (response.status != HTTP_STATUS_CREATED)
		ThrowStatusError(std::move(response),
				 "Failed to register account");

	Account account;

	auto location = response.headers.find("location");
	if (location != response.headers.end())
		account.location = std::move(location->second);

	return account;
}

AcmeChallenge
AcmeClient::NewAuthz(EVP_PKEY &key, const char *host,
		     const char *challenge_type)
{
	EnsureDirectory();
	if (directory.new_authz.empty())
		throw std::runtime_error("No new-authz in directory");

	std::string payload("{\"resource\": \"new-authz\", "
			    "\"identifier\": { "
			    "\"type\": \"dns\", "
			    "\"value\": \"");
	payload += host;
	payload += "\" } }";

	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST,
					   directory.new_authz.c_str(),
					   payload.c_str());

	if (response.status != HTTP_STATUS_CREATED)
		ThrowStatusError(std::move(response),
				 "Failed to create authz");

	const auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to create authz");

	const auto &challenge = FindInArray(root["challenges"],
					    "type", challenge_type);
	if (challenge.isNull())
		throw FormatRuntimeError("No %s challenge", challenge_type);

	const auto &token = challenge["token"];
	if (!token.isString())
		throw FormatRuntimeError("No %s token", challenge_type);

	const auto &uri = challenge["uri"];
	if (!uri.isString())
		throw FormatRuntimeError("No %s uri", challenge_type);

	return {challenge_type, token.asString(), uri.asString()};
}

bool
AcmeClient::UpdateAuthz(EVP_PKEY &key, const AcmeChallenge &authz)
{
	std::string payload("{ \"resource\": \"challenge\", "
			    "\"type\": \"");
	payload += authz.type;
	payload += "\", \"keyAuthorization\": \"";
	payload += authz.token;
	payload += '.';
	payload += UrlSafeBase64SHA256(MakeJwk(key)).c_str();
	payload += "\" }";

	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST, authz.uri.c_str(),
					   payload.c_str());

	if (response.status != HTTP_STATUS_ACCEPTED)
		ThrowStatusError(std::move(response),
				 "Failed to update authz");

	auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to update authz");
	return root["status"].asString() != "pending";
}

bool
AcmeClient::CheckAuthz(const AcmeChallenge &authz)
{
	auto response = Request(HTTP_METHOD_GET, authz.uri.c_str());
	if (response.status != HTTP_STATUS_ACCEPTED)
		ThrowStatusError(std::move(response),
				 "Failed to check authz");

	auto root = ParseJson(std::move(response));
	CheckThrowError(root, "Failed to check authz");
	return root["status"].asString() != "pending";
}

UniqueX509
AcmeClient::NewCert(EVP_PKEY &key, X509_REQ &req)
{
	EnsureDirectory();
	if (directory.new_cert.empty())
		throw std::runtime_error("No new-cert in directory");

	std::string payload("{\"resource\": \"new-cert\", "
			    "\"csr\": \"");
	payload += UrlSafeBase64(req).c_str();
	payload += "\" }";

	auto response = SignedRequestRetry(key,
					   HTTP_METHOD_POST,
					   directory.new_cert.c_str(),
					   payload.c_str());
	if (response.status != HTTP_STATUS_CREATED)
		ThrowStatusError(std::move(response),
				 "Failed to create certificate");

	return DecodeDerCertificate({response.body.data(), response.body.length()});
}
