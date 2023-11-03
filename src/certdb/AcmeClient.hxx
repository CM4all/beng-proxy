// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "event/Loop.hxx"
#include "GlueHttpClient.hxx"
#include "lib/openssl/UniqueX509.hxx"

#include <nlohmann/json_fwd.hpp>

#include <forward_list>
#include <span>
#include <string>

#include <string.h>

struct AcmeConfig;
struct AcmeAccount;
struct AcmeOrder;
struct AcmeAuthorization;
struct AcmeChallenge;

struct AcmeDirectory {
	std::string new_nonce;
	std::string new_account;
	std::string new_order;
};

/**
 * Implementation of a ACME client, i.e. the protocol of the "Let's
 * Encrypt" project.
 *
 * @see https://ietf-wg-acme.github.io/acme/
 */
class AcmeClient {
	EventLoop event_loop;
	GlueHttpClient glue_http_client;
	const std::string server;

	const std::string account_key_id;

	/**
	 * A replay nonce that was received in the previous request.  It
	 * is remembered for the next NextNonce() call, to save a HTTP
	 * request.
	 */
	std::string next_nonce;

	AcmeDirectory directory;

	const bool fake;

public:
	/**
	 * Throws on error (e.g. if CURL initialization fails).
	 */
	explicit AcmeClient(const AcmeConfig &config);

	~AcmeClient() noexcept;

	bool IsFake() const {
		return fake;
	}

	/**
	 * Register a new account.
	 *
	 * @param key the account key
	 * @param email an email address to be associated with the account
	 */
	AcmeAccount NewAccount(EVP_PKEY &key, const char *email,
			       bool only_return_existing=false);

	struct OrderRequest {
		std::forward_list<std::string> identifiers;
	};

	/**
	 * Apply for Certificate Issuance.
	 *
	 * @see https://tools.ietf.org/html/draft-ietf-acme-acme-18#section-7.3
	 *
	 * @param key the account key
	 */
	AcmeOrder NewOrder(EVP_PKEY &key, OrderRequest &&request);

	AcmeOrder FinalizeOrder(EVP_PKEY &key, const AcmeOrder &order,
				X509_REQ &csr);

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

	GlueHttpResponse FakeRequest(HttpMethod method, const char *uri,
				     std::span<const std::byte> body);

	GlueHttpResponse Request(HttpMethod method, const char *uri,
				 std::span<const std::byte> body);

	GlueHttpResponse Request(HttpMethod method, const char *uri,
				 std::nullptr_t=nullptr) {
		return Request(method, uri, std::span<const std::byte>{});
	}

	GlueHttpResponse Request(HttpMethod method, const char *uri,
				 const std::string_view body) {
		return Request(method, uri,
			       std::as_bytes(std::span<const char>{body}));
	}

	GlueHttpResponse Request(HttpMethod method, const char *uri,
				 const std::string &body) {
		return Request(method, uri,
			       static_cast<std::string_view>(body));
	}

	GlueHttpResponse Request(HttpMethod method, const char *uri,
				 const nlohmann::json &body);

	GlueHttpResponse SignedRequest(EVP_PKEY &key,
				       HttpMethod method, const char *uri,
				       std::span<const std::byte> payload);

	GlueHttpResponse SignedRequest(EVP_PKEY &key,
				       HttpMethod method, const char *uri,
				       const std::string_view body) {
		return SignedRequest(key, method, uri,
				     std::as_bytes(std::span<const char>{body}));
	}

	GlueHttpResponse SignedRequest(EVP_PKEY &key,
				       HttpMethod method, const char *uri,
				       const std::string &body) {
		return SignedRequest(key, method, uri,
				     static_cast<std::string_view>(body));
	}

	GlueHttpResponse SignedRequest(EVP_PKEY &key,
				       HttpMethod method, const char *uri,
				       const nlohmann::json &payload);

	template<typename P>
	GlueHttpResponse SignedRequestRetry(EVP_PKEY &key,
					    HttpMethod method, const char *uri,
					    P payload) {
		constexpr unsigned max_attempts = 3;
		for (unsigned remaining_attempts = max_attempts;;) {
			auto response = SignedRequest(key, method, uri, payload);
			if (!http_status_is_server_error(response.status) ||
			    --remaining_attempts == 0)
				return response;
		}
	}
};
