// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "AcmeDirectory.hxx"
#include "GlueHttpClient.hxx"
#include "lib/openssl/UniqueX509.hxx"

#include <nlohmann/json_fwd.hpp>

#include <forward_list>
#include <span>
#include <string>

#include <string.h>

struct AcmeConfig;
struct AcmeAccount;
struct AcmeOrderRequest;
struct AcmeOrder;
struct AcmeAuthorization;
struct AcmeChallenge;

/**
 * Implementation of a ACME client, i.e. the protocol of the "Let's
 * Encrypt" project.
 *
 * @see https://ietf-wg-acme.github.io/acme/
 */
class AcmeClient {
	GlueHttpClient glue_http_client;
	const char *const directory_url;

	const std::string account_key_id;

	/**
	 * A replay nonce that was received in the previous request.  It
	 * is remembered for the next NextNonce() call, to save a HTTP
	 * request.
	 */
	std::string next_nonce;

	AcmeDirectory directory;

public:
	/**
	 * Throws on error (e.g. if CURL initialization fails).
	 */
	explicit AcmeClient(const AcmeConfig &config);

	~AcmeClient() noexcept;

	/**
	 * Register a new account.
	 *
	 * @param key the account key
	 * @param email an email address to be associated with the account
	 */
	AcmeAccount NewAccount(EVP_PKEY &key, const char *email,
			       bool only_return_existing=false);

	/**
	 * Apply for Certificate Issuance.
	 *
	 * @see https://tools.ietf.org/html/draft-ietf-acme-acme-18#section-7.3
	 *
	 * @param key the account key
	 */
	AcmeOrder NewOrder(EVP_PKEY &key, AcmeOrderRequest &&request);

	AcmeOrder FinalizeOrder(EVP_PKEY &key, const AcmeOrder &order,
				const X509_REQ &csr);

	AcmeOrder PollOrder(EVP_PKEY &key, const char *url);

	UniqueX509 DownloadCertificate(EVP_PKEY &key,
				       const AcmeOrder &order);

	AcmeAuthorization Authorize(EVP_PKEY &key, const char *url);
	AcmeAuthorization PollAuthorization(EVP_PKEY &key, const char *url);

	AcmeChallenge UpdateChallenge(EVP_PKEY &key,
				      const AcmeChallenge &challenge);

private:
	/**
	 * Ask the server for a new replay nonce.
	 */
	std::string RequestNonce();

	/**
	 * Obtain a replay nonce.
	 */
	std::string NextNonce();

	void RequestDirectory();

	/**
	 * Ensure that the #AcmeDirectory is filled.
	 */
	void EnsureDirectory();

	StringCurlResponse FakeRequest(HttpMethod method, const char *uri,
				       std::span<const std::byte> body);

	StringCurlResponse Request(HttpMethod method, const char *uri,
				   std::span<const std::byte> body);

	StringCurlResponse Request(HttpMethod method, const char *uri,
				   std::nullptr_t=nullptr) {
		return Request(method, uri, std::span<const std::byte>{});
	}

	StringCurlResponse SignedRequest(EVP_PKEY &key,
					 HttpMethod method, const char *uri,
					 std::span<const std::byte> payload);

	StringCurlResponse SignedRequestRetry(EVP_PKEY &key,
					      HttpMethod method, const char *uri,
					      std::span<const std::byte> payload);
};
