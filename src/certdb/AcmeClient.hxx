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

#pragma once

#include "event/Loop.hxx"
#include "GlueHttpClient.hxx"
#include "ssl/Unique.hxx"
#include "util/ConstBuffer.hxx"

#include <forward_list>
#include <string>

#include <string.h>

namespace Json { class Value; }

struct AcmeConfig;
struct AcmeOrder;
struct AcmeAuthorization;
struct AcmeChallenge;

struct AcmeDirectory {
	std::string new_nonce;
	std::string new_account;
	std::string new_order;
	std::string new_authz;
	std::string new_cert;
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
	explicit AcmeClient(const AcmeConfig &config) noexcept;
	~AcmeClient() noexcept;

	bool IsFake() const {
		return fake;
	}

	struct Account {
		std::string location;
	};

	/**
	 * Register a new account.
	 *
	 * @param key the account key
	 * @param email an email address to be associated with the account
	 */
	Account NewAccount(EVP_PKEY &key, const char *email,
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

	GlueHttpResponse FakeRequest(http_method_t method, const char *uri,
				     ConstBuffer<void> body);

	GlueHttpResponse Request(http_method_t method, const char *uri,
				 ConstBuffer<void> body);

	GlueHttpResponse Request(http_method_t method, const char *uri,
				 std::nullptr_t body=nullptr) {
		return Request(method, uri, ConstBuffer<void>{body});
	}

	GlueHttpResponse Request(http_method_t method, const char *uri,
				 const std::string &body) {
		return Request(method, uri,
			       ConstBuffer<void>{body.data(), body.size()});
	}

	GlueHttpResponse Request(http_method_t method, const char *uri,
				 const Json::Value &body);

	GlueHttpResponse SignedRequest(EVP_PKEY &key,
				       http_method_t method, const char *uri,
				       ConstBuffer<void> payload);

	GlueHttpResponse SignedRequest(EVP_PKEY &key,
				       http_method_t method, const char *uri,
				       const char *payload) {
		return SignedRequest(key, method, uri,
				     ConstBuffer<void>(payload, strlen(payload)));
	}

	GlueHttpResponse SignedRequest(EVP_PKEY &key,
				       http_method_t method, const char *uri,
				       const Json::Value &payload);

	template<typename P>
	GlueHttpResponse SignedRequestRetry(EVP_PKEY &key,
					    http_method_t method, const char *uri,
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
