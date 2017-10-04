/*
 * Copyright 2007-2017 Content Management AG
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
#include "AcmeError.hxx"
#include "AcmeConfig.hxx"
#include "ssl/Base64.hxx"
#include "ssl/Certificate.hxx"
#include "ssl/Key.hxx"
#include "uri/uri_extract.hxx"
#include "util/Exception.hxx"

#include <json/json.h>

#include <memory>
#include <sstream>

gcc_pure
static bool
IsJson(const GlueHttpResponse &response)
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
ParseJson(std::string &&s)
{
    Json::Value root;
    std::stringstream(std::move(s)) >> root;
    return root;
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

/**
 * Check the status, and if it's not the expected one, throw an
 * exception.
 */
static void
CheckThrowStatusError(GlueHttpResponse &&response,
                      http_status_t expected_status,
                      const char *msg)
{
    if (response.status != expected_status)
        ThrowStatusError(std::move(response), msg);
}

AcmeClient::AcmeClient(const AcmeConfig &config)
    :glue_http_client(event_loop),
     server(config.staging
            ? "https://acme-staging.api.letsencrypt.org"
            : "https://acme-v01.api.letsencrypt.org"),
     agreement_url(config.agreement_url),
     fake(config.fake)
{
}

AcmeClient::~AcmeClient()
{
}

std::string
AcmeClient::RequestNonce()
{
    if (fake)
        return "foo";

    auto response = glue_http_client.Request(event_loop,
                                             HTTP_METHOD_HEAD,
                                             (server + "/directory").c_str(),
                                             nullptr);
    if (response.status != HTTP_STATUS_OK)
        throw std::runtime_error("Unexpected response status");
    auto nonce = response.headers.find("replay-nonce");
    if (nonce == response.headers.end())
        throw std::runtime_error("No Replay-Nonce response header");
    return nonce->second.c_str();
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

static std::string
MakeJwk(EVP_PKEY &key)
{
    if (EVP_PKEY_base_id(&key) != EVP_PKEY_RSA)
        throw std::runtime_error("RSA key expected");

    const BIGNUM *n, *e;
#if OPENSSL_VERSION_NUMBER >= 0x10100000L
    RSA_get0_key(EVP_PKEY_get0_RSA(&key), &n, &e, nullptr);
#else
    n = key.pkey.rsa->n;
    e = key.pkey.rsa->e;
#endif

    const auto exponent = UrlSafeBase64(*e);
    const auto modulus = UrlSafeBase64(*n);
    std::string jwk("{\"e\":\"");
    jwk += exponent.c_str();
    jwk += "\",\"kty\":\"RSA\",\"n\":\"";
    jwk += modulus.c_str();
    jwk += "\"}";
    return jwk;
}

static std::string
MakeHeader(EVP_PKEY &key)
{
    auto jwk = MakeJwk(key);

    std::string header("{\"alg\": \"RS256\", \"jwk\": ");
    header += jwk;
    header += "}";
    return header;
}

static std::string
WithNonce(const std::string &_header, const std::string &nonce)
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
                                   method, (server + uri).c_str(),
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

AcmeClient::Account
AcmeClient::NewReg(EVP_PKEY &key, const char *email)
{
    std::string payload("{\"resource\": \"new-reg\", ");

    if (email != nullptr) {
        payload += "\"contact\": [ \"mailto:";
        payload += email;
        payload += "\" ], ";
    }

    payload += "\"agreement\": \"";
    payload += agreement_url;
    payload += "\"}";

    auto response = SignedRequest(key,
                                  HTTP_METHOD_POST, "/acme/new-reg",
                                  payload.c_str());
    CheckThrowStatusError(std::move(response), HTTP_STATUS_CREATED,
                          "Failed to register account");

    Account account;

    auto location = response.headers.find("location");
    if (location != response.headers.end())
        account.location = std::move(location->second);

    return account;
}

static const Json::Value &
FindInArray(const Json::Value &v, const char *key, const char *value)
{
    for (const auto &i : v) {
        const auto &l = i[key];
        if (!l.isNull() && l.asString() == value)
            return i;
    }

    return Json::Value::null;
}

AcmeClient::AuthzTlsSni01
AcmeClient::NewAuthz(EVP_PKEY &key, const char *host)
{
    std::string payload("{\"resource\": \"new-authz\", "
                        "\"identifier\": { "
                        "\"type\": \"dns\", "
                        "\"value\": \"");
    payload += host;
    payload += "\" } }";

    auto response = SignedRequest(key,
                                  HTTP_METHOD_POST, "/acme/new-authz",
                                  payload.c_str());
    CheckThrowStatusError(std::move(response), HTTP_STATUS_CREATED,
                          "Failed to create authz");

    const auto root = ParseJson(std::move(response));
    CheckThrowError(root, "Failed to create authz");

    const auto &tls_sni_01 = FindInArray(root["challenges"],
                                         "type", "tls-sni-01");
    if (tls_sni_01.isNull())
        throw std::runtime_error("No tls-sni-01 challenge");

    const auto &token = tls_sni_01["token"];
    if (!token.isString())
        throw std::runtime_error("No tls-sni-01 token");

    const auto &uri = tls_sni_01["uri"];
    if (!uri.isString())
        throw std::runtime_error("No tls-sni-01 uri");

    return {token.asString(), uri.asString()};
}

bool
AcmeClient::UpdateAuthz(EVP_PKEY &key, const AuthzTlsSni01 &authz)
{
    const char *uri = uri_path(authz.uri.c_str());
    if (uri == nullptr)
        throw std::runtime_error("Malformed URI in AuthzTlsSni01");

    std::string payload("{ \"resource\": \"challenge\", "
                        "\"type\": \"tls-sni-01\", "
                        "\"keyAuthorization\": \"");
    payload += authz.token;
    payload += '.';
    payload += UrlSafeBase64SHA256(MakeJwk(key)).c_str();
    payload += "\" }";

    auto response = SignedRequest(key,
                                  HTTP_METHOD_POST, uri,
                                  payload.c_str());
    CheckThrowStatusError(std::move(response), HTTP_STATUS_ACCEPTED,
                          "Failed to update authz");

    auto root = ParseJson(std::move(response));
    CheckThrowError(root, "Failed to update authz");
    return root["status"].asString() != "pending";
}

bool
AcmeClient::CheckAuthz(const AuthzTlsSni01 &authz)
{
    const char *uri = uri_path(authz.uri.c_str());
    if (uri == nullptr)
        throw std::runtime_error("Malformed URI in AuthzTlsSni01");

    auto response = Request(HTTP_METHOD_GET, uri,
                            nullptr);
    CheckThrowStatusError(std::move(response), HTTP_STATUS_ACCEPTED,
                          "Failed to check authz");

    auto root = ParseJson(std::move(response));
    CheckThrowError(root, "Failed to check authz");
    return root["status"].asString() != "pending";
}

UniqueX509
AcmeClient::NewCert(EVP_PKEY &key, X509_REQ &req)
{
    std::string payload("{\"resource\": \"new-cert\", "
                        "\"csr\": \"");
    payload += UrlSafeBase64(req).c_str();
    payload += "\" }";

    auto response = SignedRequest(key,
                                  HTTP_METHOD_POST, "/acme/new-cert",
                                  payload.c_str());
    CheckThrowStatusError(std::move(response), HTTP_STATUS_CREATED,
                          "Failed to create certificate");

    return DecodeDerCertificate({response.body.data(), response.body.length()});
}

static char *
Hex(char *dest, ConstBuffer<uint8_t> src)
{
    for (auto b : src)
        dest += sprintf(dest, "%02x", b);
    return dest;
}

std::string
AcmeClient::AuthzTlsSni01::MakeDnsName(EVP_PKEY &key) const
{
    const auto thumbprint_b64 = UrlSafeBase64SHA256(MakeJwk(key));

    std::string key_authz = token;
    key_authz += '.';
    key_authz += thumbprint_b64.c_str();

    unsigned char md[SHA256_DIGEST_LENGTH];
    SHA256((const unsigned char *)key_authz.data(), key_authz.length(), md);

    char result[SHA256_DIGEST_LENGTH * 2 + 32], *p = result;
    p = Hex(p, {md, SHA256_DIGEST_LENGTH / 2});
    *p++ = '.';
    p = Hex(p, {md + SHA256_DIGEST_LENGTH / 2, SHA256_DIGEST_LENGTH / 2});
    strcpy(p, ".acme.invalid");

    return result;
}
