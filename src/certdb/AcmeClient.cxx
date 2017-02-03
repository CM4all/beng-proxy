/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "AcmeClient.hxx"
#include "direct.hxx"
#include "pool.hxx"
#include "ssl/Base64.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Dummy.hxx"
#include "ssl/Key.hxx"
#include "uri/uri_extract.hxx"
#include "util/ScopeExit.hxx"

#include <json/json.h>

#include <memory>
#include <sstream>

gcc_pure
static bool
IsJson(const GlueHttpResponse &response)
{
    const char *content_type = response.headers.Get("content-type");
    return content_type != nullptr &&
        (strcmp(content_type, "application/json") == 0 ||
         strcmp(content_type, "application/problem+json") == 0);
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

    std::string what(msg);
    what += ": [";
    what += error["type"].asString();
    what += "] ";
    what += error["detail"].asString();
    throw std::runtime_error(std::move(what));
}

/**
 * Throw an exception, adding "detail" from the JSON document (if the
 * response is JSON).
 */
gcc_noreturn
static void
ThrowError(GlueHttpResponse &&response, const char *msg)
{
    std::string what(msg);

    if (IsJson(response)) {
        const auto root = ParseJson(std::move(response.body));
        what += ": [";
        what += root["type"].asString();
        what += "] ";
        what += root["detail"].asString();
    }

    throw std::runtime_error(std::move(what));
}

AcmeClient::AcmeClient(bool staging, bool _fake)
    :glue_http_client(event_loop),
     server(true,
            staging
            ? "acme-staging.api.letsencrypt.org"
            : "acme-v01.api.letsencrypt.org",
            443),
     fake(_fake)
{
    direct_global_init();
}

AcmeClient::~AcmeClient()
{
}

std::string
AcmeClient::RequestNonce()
{
    if (fake)
        return "foo";

    LinearPool pool(root_pool, "RequestNonce", 8192);
    auto response = glue_http_client.Request(event_loop, pool, server,
                                             HTTP_METHOD_HEAD, "/directory",
                                             HttpHeaders(pool), nullptr);
    if (response.status != HTTP_STATUS_OK)
        throw std::runtime_error("Unexpected response status");
    const char *nonce = response.headers.Get("replay-nonce");
    if (nonce == nullptr)
        throw std::runtime_error("No Replay-Nonce response header");
    return nonce;
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
AcmeClient::Request(struct pool &p,
                    http_method_t method, const char *uri,
                    HttpHeaders &&headers,
                    ConstBuffer<void> body)
{
    if (fake) {
        if (strcmp(uri, "/acme/new-authz") == 0) {
            StringMap response_headers(p);
            response_headers.Add("content-type", "application/json");

            return GlueHttpResponse(HTTP_STATUS_CREATED,
                                    std::move(response_headers),
                                    "{"
                                    "  \"status\": \"pending\","
                                    "  \"identifier\": {\"type\": \"dns\", \"value\": \"example.org\"},"
                                    "  \"challenges\": ["
                                    "    {"
                                    "      \"type\": \"tls-sni-01\","
                                    "      \"token\": \"example-token-tls-sni-01\","
                                    "      \"uri\": \"http://xyz/example/tls-sni-01/uri\""
                                    "    }"
                                    "  ]"
                                    "}");
        } else if (strcmp(uri, "/example/tls-sni-01/uri") == 0) {
            StringMap response_headers(p);
            response_headers.Add("content-type", "application/json");

            return GlueHttpResponse(HTTP_STATUS_ACCEPTED,
                                    std::move(response_headers),
                                    "{"
                                    "  \"status\": \"valid\""
                                    "}");
        } else if (strcmp(uri, "/acme/new-cert") == 0) {
            auto key = GenerateRsaKey();
            auto cert = MakeSelfSignedDummyCert(*key, "example.com");

            const SslBuffer cert_buffer(*cert);

            auto response_body = ConstBuffer<char>::FromVoid(cert_buffer.get());
            return GlueHttpResponse(HTTP_STATUS_CREATED, StringMap(p),
                                    std::string(response_body.data,
                                                response_body.size));
        } else
            return GlueHttpResponse(HTTP_STATUS_NOT_FOUND, StringMap(p),
                                    "Not found");
    }

    auto response = glue_http_client.Request(event_loop, p, server,
                                             method, uri, std::move(headers),
                                             body);
    const char *new_nonce = response.headers.Get("replay-nonce");
    if (new_nonce != nullptr)
        next_nonce = new_nonce;

    return response;
}

GlueHttpResponse
AcmeClient::SignedRequest(struct pool &p, EVP_PKEY &key,
                          http_method_t method, const char *uri,
                          HttpHeaders &&headers,
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

    return Request(p, method, uri, std::move(headers),
                   {body.data(), body.length()});
}

AcmeClient::Account
AcmeClient::NewReg(EVP_PKEY &key, const char *email)
{
    LinearPool p(root_pool, "new-authz", 8192);

    std::string payload("{\"resource\": \"new-reg\", ");

    if (email != nullptr) {
        payload += "\"contact\": [ \"mailto:";
        payload += email;
        payload += "\" ], ";
    }

    payload += "\"agreement\": \"https://letsencrypt.org/documents/LE-SA-v1.0.1-July-27-2015.pdf\"}";

    auto response = SignedRequest(p, key,
                                  HTTP_METHOD_POST, "/acme/new-reg",
                                  HttpHeaders(p),
                                  payload.c_str());
    if (response.status != HTTP_STATUS_CREATED)
        ThrowError(std::move(response), "Failed to register account");

    Account account;

    const char *location = response.headers.Get("location");
    if (location != nullptr)
        account.location = location;

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
    LinearPool p(root_pool, "new-authz", 8192);

    std::string payload("{\"resource\": \"new-authz\", "
                        "\"identifier\": { "
                        "\"type\": \"dns\", "
                        "\"value\": \"");
    payload += host;
    payload += "\" } }";

    auto response = SignedRequest(p, key,
                                  HTTP_METHOD_POST, "/acme/new-authz",
                                  HttpHeaders(p),
                                  payload.c_str());
    if (response.status != HTTP_STATUS_CREATED)
        ThrowError(std::move(response), "Failed to create authz");

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
    LinearPool p(root_pool, "update-authz", 8192);

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

    auto response = SignedRequest(p, key,
                                  HTTP_METHOD_POST, uri,
                                  HttpHeaders(p),
                                  payload.c_str());
    if (response.status != HTTP_STATUS_ACCEPTED)
        ThrowError(std::move(response), "Failed to update authz");

    auto root = ParseJson(std::move(response));
    CheckThrowError(root, "Failed to update authz");
    return root["status"].asString() != "pending";
}

bool
AcmeClient::CheckAuthz(const AuthzTlsSni01 &authz)
{
    LinearPool p(root_pool, "check-authz", 8192);

    const char *uri = uri_path(authz.uri.c_str());
    if (uri == nullptr)
        throw std::runtime_error("Malformed URI in AuthzTlsSni01");

    auto response = Request(p, HTTP_METHOD_GET, uri,
                            HttpHeaders(p), nullptr);
    if (response.status != HTTP_STATUS_ACCEPTED)
        ThrowError(std::move(response), "Failed to check authz");

    auto root = ParseJson(std::move(response));
    CheckThrowError(root, "Failed to check authz");
    return root["status"].asString() != "pending";
}

UniqueX509
AcmeClient::NewCert(EVP_PKEY &key, X509_REQ &req)
{
    LinearPool p(root_pool, "new-cert", 8192);

    std::string payload("{\"resource\": \"new-cert\", "
                        "\"csr\": \"");
    payload += UrlSafeBase64(req).c_str();
    payload += "\" }";

    auto response = SignedRequest(p, key,
                                  HTTP_METHOD_POST, "/acme/new-cert",
                                  HttpHeaders(p),
                                  payload.c_str());
    if (response.status != HTTP_STATUS_CREATED)
        ThrowError(std::move(response), "Failed to create certificate");

    auto data = (const unsigned char *)response.body.data();
    UniqueX509 cert(d2i_X509(nullptr, &data, response.body.length()));
    if (!cert)
        throw "d2i_X509() failed";

    return cert;
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
