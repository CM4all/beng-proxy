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

#include <string>

#include <string.h>

namespace Json { class Value; }

struct AcmeConfig;
struct AcmeChallenge;

struct AcmeDirectory {
	std::string new_reg;
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

	/**
	 * A replay nonce that was received in the previous request.  It
	 * is remembered for the next NextNonce() call, to save a HTTP
	 * request.
	 */
	std::string next_nonce;

	AcmeDirectory directory;

	const std::string agreement_url;

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
	Account NewReg(EVP_PKEY &key, const char *email);

	/**
	 * Create a new "authz" object, to prepare for a new certificate.
	 *
	 * After this method succeeds, configure the web server with a new
	 * temporary certificate using AcmeChallenge::MakeDnsName(), and
	 * then call UpdateAuthz().
	 *
	 * @param key the account key
	 * @param host the host name ("common name") for the new certificate
	 * @param challenge_type the desired challenge type
	 */
	AcmeChallenge NewAuthz(EVP_PKEY &key, const char *host,
			       const char *challenge_type);

	/**
	 * Update the "authz" object.  Call this method after NewAuthz().
	 *
	 * If this method returns false, call CheckAuthz() repeatedly with
	 * a reasonable delay.
	 *
	 * @param key the account key
	 * @param authz the return value of NewAuthz()
	 * @return true if the authz object is done, and NewCert() can be
	 * called
	 */
	bool UpdateAuthz(EVP_PKEY &key, const AcmeChallenge &authz);

	/**
	 * Check whether the "authz" object is done.  Call this method
	 * repeatedly after UpdateAuthz() with a reasonable delay.
	 *
	 * @param key the account key
	 * @param authz the return value of NewAuthz()
	 * @return true if the authz object is done, and NewCert() can be
	 * called
	 */
	bool CheckAuthz(const AcmeChallenge &authz);

	/**
	 * Ask the server to produce a signed certificate.
	 *
	 * @param key the account key
	 * @param req the certificate request signed with the certificate
	 * key (not with the account key!)
	 */
	UniqueX509 NewCert(EVP_PKEY &key, X509_REQ &req);

private:
	/**
	 * Ask the server for a new replay nonce.
	 */
	std::string RequestNonce();

	/**
	 * Obtain a replay nonce.
	 */
	std::string NextNonce();

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
