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
#include "ssl/Request.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Key.hxx"
#include "ssl/Error.hxx"
#include "util/AllocatedArray.hxx"
#include "util/Exception.hxx"

#include <json/json.h>

#include <sstream>

static Json::Value
ParseJson(ConstBuffer<void> buffer)
{
	Json::Value root;
	std::stringstream(std::string((const char *)buffer.data, buffer.size)) >> root;
	return root;
}

template<typename T>
static Json::Value
ParseJson(const AllocatedArray<T> &src)
{
	return ParseJson(ConstBuffer<T>(&src.front(), src.size()).ToVoid());
}

/*
  static Json::Value
  ParseJson(const std::string &src)
  {
  return ParseJson(ConstBuffer<void>(src.data(), src.length()));
  }
*/

static void
DecodeUrlSafe(BIO *dest, const char *src)
{
	while (*src != 0) {
		char ch = *src++;
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

static AllocatedArray<char>
DecodeUrlSafeBase64(const char *src)
{
	const size_t src_length = strlen(src);

	UniqueBIO mem_bio(BIO_new(BIO_s_mem()));
	DecodeUrlSafe(mem_bio.get(), src);

	/* add padding */
	switch (src_length % 4) {
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

	const size_t buffer_size = src_length;
	AllocatedArray<char> buffer(buffer_size);
	int nbytes = BIO_read(b, &buffer.front(), buffer_size);
	buffer.SetSize(std::max(nbytes, 0));
	return buffer;
}

static AllocatedArray<char>
ParseSignedBody(ConstBuffer<void> body)
{
	return DecodeUrlSafeBase64(ParseJson(body)["payload"].asString().c_str());
}

static UniqueX509_REQ
ParseNewCertBody(ConstBuffer<void> body)
{
	const auto payload = ParseJson(ParseSignedBody(body));
	const auto req_der = DecodeUrlSafeBase64(payload["csr"].asString().c_str());
	return DecodeDerCertificateRequest(ConstBuffer<void>(&req_der.front(), req_der.size()));
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
			ConstBuffer<void> body)
try {
	(void)method;
	(void)body;

	if (strcmp(uri, "/acme/new-authz") == 0) {
		std::multimap<std::string, std::string> response_headers = {
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
		std::multimap<std::string, std::string> response_headers = {
			{"content-type", "application/json"},
		};

		return GlueHttpResponse(HTTP_STATUS_ACCEPTED,
					std::move(response_headers),
					"{"
					"  \"status\": \"valid\""
					"}");
	} else if (strcmp(uri, "/acme/new-cert") == 0) {
		if (method != HTTP_METHOD_POST || body == nullptr)
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

		X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
		X509_gmtime_adj(X509_get_notAfter(cert.get()), 60 * 60);

		CopyExtensions(*cert, *req);

		X509_set_pubkey(cert.get(), pkey.release());

		auto key = GenerateRsaKey();
		if (!X509_sign(cert.get(), key.get(), EVP_sha1()))
			throw SslError("X509_sign() failed");

		const SslBuffer cert_buffer(*cert);

		auto response_body = ConstBuffer<char>::FromVoid(cert_buffer.get());
		return GlueHttpResponse(HTTP_STATUS_CREATED, {},
					std::string(response_body.data,
						    response_body.size));
	} else
		return GlueHttpResponse(HTTP_STATUS_NOT_FOUND, {},
					"Not found");
} catch (...) {
	return GlueHttpResponse(HTTP_STATUS_INTERNAL_SERVER_ERROR, {},
				GetFullMessage(std::current_exception()));
}
