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

#include "AcmeClient.hxx"
#include "lib/openssl/Request.hxx"
#include "lib/openssl/Buffer.hxx"
#include "lib/openssl/Key.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/UniqueBIO.hxx"
#include "util/AllocatedArray.hxx"
#include "util/Exception.hxx"

#include <boost/json.hpp>

#include <sstream>

static constexpr std::string_view
ToStringView(std::span<const std::byte> span) noexcept
{
	const std::span<const char> char_span{(const char *)(const void *)span.data(), span.size()};
	return {char_span.begin(), char_span.end()};
}

static boost::json::value
ParseJson(std::span<const std::byte> buffer)
{
	return boost::json::parse(ToStringView(buffer));
}

template<typename T>
static boost::json::value
ParseJson(const AllocatedArray<T> &src)
{
	return ParseJson(std::as_bytes(std::span<const T>(src)));
}

/*
  static Json::Value
  ParseJson(const std::string &src)
  {
  return ParseJson(ConstBuffer<void>(src.data(), src.length()));
  }
*/

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
	const auto &payload = root.as_object().at("playload");
	return DecodeUrlSafeBase64(payload.as_string());
}

static UniqueX509_REQ
ParseNewCertBody(std::span<const std::byte> body)
{
	const auto payload = ParseJson(ParseSignedBody(body));
	const auto &csr = payload.as_object().at("csr");
	const auto req_der = DecodeUrlSafeBase64(csr.as_string());
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
AcmeClient::FakeRequest(http_method_t method, const char *uri,
			std::span<const std::byte> body)
try {
	(void)method;
	(void)body;

	if (strcmp(uri, "/acme/new-authz") == 0) {
		Curl::Headers response_headers = {
			{"content-type", "application/json"},
		};

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
		Curl::Headers response_headers = {
			{"content-type", "application/json"},
		};

		return GlueHttpResponse(HTTP_STATUS_ACCEPTED,
					std::move(response_headers),
					"{"
					"  \"status\": \"valid\""
					"}");
	} else if (strcmp(uri, "/acme/new-cert") == 0) {
		if (method != HTTP_METHOD_POST || body.data() == nullptr)
			return GlueHttpResponse(HTTP_STATUS_BAD_REQUEST, {},
						"Bad request");

		const auto req = ParseNewCertBody(body);

		UniqueEVP_PKEY pkey(X509_REQ_get_pubkey(req.get()));
		if (!pkey)
			return GlueHttpResponse(HTTP_STATUS_BAD_REQUEST, {},
						"No public key");

		if (X509_REQ_verify(req.get(), pkey.get()) < 0)
			return GlueHttpResponse(HTTP_STATUS_BAD_REQUEST, {},
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

		auto key = GenerateRsaKey();
		if (!X509_sign(cert.get(), key.get(), EVP_sha1()))
			throw SslError("X509_sign() failed");

		const SslBuffer cert_buffer(*cert);

		return GlueHttpResponse(HTTP_STATUS_CREATED, {},
					std::string{ToStringView(cert_buffer.get())});
	} else
		return GlueHttpResponse(HTTP_STATUS_NOT_FOUND, {},
					"Not found");
} catch (...) {
	return GlueHttpResponse(HTTP_STATUS_INTERNAL_SERVER_ERROR, {},
				GetFullMessage(std::current_exception()));
}
