// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AcmeClient.hxx"
#include "lib/openssl/Request.hxx"
#include "lib/openssl/Buffer.hxx"
#include "lib/openssl/Key.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/UniqueBIO.hxx"
#include "http/Status.hxx"
#include "http/Method.hxx"
#include "util/AllocatedArray.hxx"
#include "util/Exception.hxx"

#include <nlohmann/json.hpp>

#include <sstream>

using std::string_view_literals::operator""sv;
using json = nlohmann::json;

static constexpr std::string_view
ToStringView(std::span<const std::byte> span) noexcept
{
	const std::span<const char> char_span{(const char *)(const void *)span.data(), span.size()};
	return {char_span.begin(), char_span.end()};
}

static json
ParseJson(std::span<const std::byte> buffer)
{
	return json::parse(ToStringView(buffer));
}

template<typename T>
static json
ParseJson(const AllocatedArray<T> &src)
{
	return ParseJson(std::as_bytes(std::span<const T>(src)));
}

static void
DecodeUrlSafe(BIO *dest, std::string_view src)
{
	for (char ch : src) {
		switch (ch) {
		case '-':
			ch = '+';
			break;

		case '_':
			ch = '/';
			break;
		}

		BIO_write(dest, &ch, 1);
	}
}

static AllocatedArray<std::byte>
DecodeUrlSafeBase64(std::string_view src)
{
	UniqueBIO mem_bio(BIO_new(BIO_s_mem()));
	DecodeUrlSafe(mem_bio.get(), src);

	/* add padding */
	switch (src.size() % 4) {
	case 2:
		BIO_write(mem_bio.get(), "==", 2);
		break;

	case 3:
		BIO_write(mem_bio.get(), "==", 1);
		break;
	}

	UniqueBIO b64(BIO_new(BIO_f_base64()));
	BIO_set_flags(b64.get(), BIO_FLAGS_BASE64_NO_NL);
	BIO *b = BIO_push(b64.get(), mem_bio.get());

	const size_t buffer_size = src.size();
	AllocatedArray<std::byte> buffer{buffer_size};
	int nbytes = BIO_read(b, buffer.data(), buffer_size);
	buffer.SetSize(std::max(nbytes, 0));
	return buffer;
}

static AllocatedArray<std::byte>
ParseSignedBody(std::span<const std::byte> body)
{
	const auto root = ParseJson(body);
	return DecodeUrlSafeBase64(root.at("payload"sv).get<std::string_view>());
}

static UniqueX509_REQ
ParseNewCertBody(std::span<const std::byte> body)
{
	const auto payload = ParseJson(ParseSignedBody(body));
	const auto &csr = payload.at("csr"sv);
	const auto req_der = DecodeUrlSafeBase64(csr.get<std::string_view>());
	return DecodeDerCertificateRequest(req_der);
}

static void
CopyExtensions(X509 &dest, X509_EXTENSIONS &src)
{
	const unsigned n = sk_X509_EXTENSION_num(&src);
	for (unsigned i = 0; i < n; ++i) {
		auto *ext = sk_X509_EXTENSION_value(&src, i);
		if (!X509_add_ext(&dest, ext, -1))
			throw SslError("X509_add_ext() failed");
	}
}

static void
CopyExtensions(X509 &dest, X509_REQ &src)
{
	UniqueX509_EXTENSIONS exts(X509_REQ_get_extensions(&src));
	if (exts)
		CopyExtensions(dest, *exts);
}

GlueHttpResponse
AcmeClient::FakeRequest(HttpMethod method, const char *uri,
			std::span<const std::byte> body)
try {
	(void)method;
	(void)body;

	if (strcmp(uri, "/acme/new-authz") == 0) {
		Curl::Headers response_headers = {
			{"content-type", "application/json"},
		};

		return GlueHttpResponse(HttpStatus::CREATED,
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
		Curl::Headers response_headers = {
			{"content-type", "application/json"},
		};

		return GlueHttpResponse(HttpStatus::ACCEPTED,
					std::move(response_headers),
					"{"
					"  \"status\": \"valid\""
					"}");
	} else if (strcmp(uri, "/acme/new-cert") == 0) {
		if (method != HttpMethod::POST || body.data() == nullptr)
			return GlueHttpResponse(HttpStatus::BAD_REQUEST, {},
						"Bad request");

		const auto req = ParseNewCertBody(body);

		UniqueEVP_PKEY pkey(X509_REQ_get_pubkey(req.get()));
		if (!pkey)
			return GlueHttpResponse(HttpStatus::BAD_REQUEST, {},
						"No public key");

		if (X509_REQ_verify(req.get(), pkey.get()) < 0)
			return GlueHttpResponse(HttpStatus::BAD_REQUEST, {},
						"Request verification failed");

		UniqueX509 cert(X509_new());
		ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 42);

		if (!X509_set_issuer_name(cert.get(), X509_REQ_get_subject_name(req.get())))
			throw SslError("X509_set_issuer_name() failed");

		if (!X509_set_subject_name(cert.get(), X509_REQ_get_subject_name(req.get())))
			throw SslError("X509_set_subject_name() failed");

		X509_gmtime_adj(X509_getm_notBefore(cert.get()), 0);
		X509_gmtime_adj(X509_getm_notAfter(cert.get()), 60 * 60);

		CopyExtensions(*cert, *req);

		X509_set_pubkey(cert.get(), pkey.release());

		auto key = GenerateEcKey();
		if (!X509_sign(cert.get(), key.get(), EVP_sha256()))
			throw SslError("X509_sign() failed");

		const SslBuffer cert_buffer(*cert);

		return GlueHttpResponse(HttpStatus::CREATED, {},
					std::string{ToStringView(cert_buffer.get())});
	} else
		return GlueHttpResponse(HttpStatus::NOT_FOUND, {},
					"Not found");
} catch (...) {
	return GlueHttpResponse(HttpStatus::INTERNAL_SERVER_ERROR, {},
				GetFullMessage(std::current_exception()));
}
