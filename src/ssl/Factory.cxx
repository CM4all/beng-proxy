/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "Factory.hxx"
#include "Basic.hxx"
#include "Config.hxx"
#include "CertCallback.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/Name.hxx"
#include "lib/openssl/AltName.hxx"
#include "lib/openssl/LoadFile.hxx"
#include "lib/openssl/Key.hxx"
#include "lib/openssl/UniqueEVP.hxx"
#include "lib/openssl/UniqueX509.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <algorithm>
#include <forward_list>

#include <assert.h>
#include <string.h>

template<typename Name>
[[gnu::pure]]
static auto
GetCertificateNames(X509 &cert) noexcept
{
	std::forward_list<Name> names;

	if (X509_NAME *subject = X509_get_subject_name(&cert);
	    subject != nullptr) {
		if (auto common_name = NidToString(*subject, NID_commonName);
		    common_name != nullptr)
			names.emplace_front(std::move(common_name));
	}

	for (const auto &i : GetSubjectAltNames(cert))
		names.emplace_front(std::string_view{i});

	return names;
}

struct SslFactoryCertKey {
	struct Name {
		AllocatedString value;
		size_t length;

		Name(AllocatedString &&_value) noexcept
			:value(std::move(_value)), length(strlen(value.c_str())) {}

		Name(std::string_view _value) noexcept
			:value(_value),
			 length(_value.size()) {}

		[[gnu::pure]]
		bool Match(std::string_view host_name) const noexcept;
	};

	UniqueX509 cert;
	std::forward_list<UniqueX509> chain;
	UniqueEVP_PKEY key;

	std::forward_list<Name> names;

	explicit SslFactoryCertKey(const SslCertKeyConfig &config);

	SslFactoryCertKey(SslFactoryCertKey &&other) = default;
	SslFactoryCertKey &operator=(SslFactoryCertKey &&other) = default;

	[[gnu::pure]]
	bool MatchCommonName(std::string_view host_name) const noexcept;

	void Apply(SSL_CTX &ssl_ctx) const noexcept {
		SSL_CTX_use_PrivateKey(&ssl_ctx, key.get());
		SSL_CTX_use_certificate(&ssl_ctx, cert.get());

		for (const auto &i : chain)
			SSL_CTX_add1_chain_cert(&ssl_ctx, i.get());
	}

	void Apply(SSL &ssl) const noexcept {
		SSL_use_PrivateKey(&ssl, key.get());
		SSL_use_certificate(&ssl, cert.get());

		for (const auto &i : chain)
			SSL_add1_chain_cert(&ssl, i.get());
	}
};

inline bool
SslFactoryCertKey::Name::Match(std::string_view host_name) const noexcept
{
	if (value == nullptr)
		return false;

	if (length == host_name.size() &&
	    memcmp(host_name.data(), value.c_str(), host_name.size()) == 0)
		return true;

	if (value[0] == '*' && value[1] == '.' && value[2] != 0) {
		if (host_name.size() >= length &&
		    /* match only one segment (no dots) */
		    memchr(host_name.data(), '.',
			   host_name.size() - length + 1) == nullptr &&
		    memcmp(host_name.data() + host_name.size() - length + 1,
			   value.c_str() + 1, length - 1) == 0)
			return true;
	}

	return false;
}

inline bool
SslFactoryCertKey::MatchCommonName(std::string_view host_name) const noexcept
{
	for (const auto &name : names)
		if (name.Match(host_name))
			return true;

	return false;
}

SslFactory::SslFactory(const SslConfig &config,
		       std::unique_ptr<SslCertCallback> _cert_callback)
	:ssl_ctx(CreateBasicSslCtx(true)),
	 cert_callback(std::move(_cert_callback))
{
	assert(!config.cert_key.empty());

	ApplyServerConfig(*ssl_ctx, config);

	cert_key.reserve(config.cert_key.size());
	for (const auto &c : config.cert_key)
		cert_key.emplace_back(c);

	if (cert_key.size() > 1 || cert_callback)
		SSL_CTX_set_cert_cb(ssl_ctx.get(), CertCallback, this);
	else if (!cert_key.empty())
		cert_key.front().Apply(*ssl_ctx);
}

SslFactory::~SslFactory() noexcept = default;

inline const SslFactoryCertKey *
SslFactory::FindCommonName(std::string_view host_name) const noexcept
{
	for (const auto &ck : cert_key)
		if (ck.MatchCommonName(host_name))
			return &ck;

	return nullptr;
}

inline int
SslFactory::CertCallback(SSL &ssl) noexcept
{
	const char *_host_name = SSL_get_servername(&ssl, TLSEXT_NAMETYPE_host_name);
	if (_host_name == nullptr) {
		if (!cert_key.empty())
			cert_key.front().Apply(ssl);

		return 1;
	}

	const std::string_view host_name{_host_name};

	/* find the first certificate that matches */

	if (const auto *ck = FindCommonName(host_name)) {
		/* found it - now use it */
		ck->Apply(ssl);
		return 1;
	}

	/* check the certificate database */

	if (cert_callback) {
		switch (cert_callback->OnCertCallback(ssl, host_name.data())) {
		case LookupCertResult::NOT_FOUND:
			break;

		case LookupCertResult::COMPLETE:
			return 1;

		case LookupCertResult::IN_PROGRESS:
			return -1;

		case LookupCertResult::ERROR:
			// abort the handshake
			return 0;
		}
	}

	/* no match: fall back to the first configured certificate (if
	   there is one) */

	if (!cert_key.empty())
		cert_key.front().Apply(ssl);

	return 1;
}

int
SslFactory::CertCallback(SSL *ssl, void *arg) noexcept
{
	auto &f = *(SslFactory *)arg;
	return f.CertCallback(*ssl);
}

void
SslFactory::SetSessionIdContext(std::span<const std::byte> sid_ctx)
{
	int result = SSL_CTX_set_session_id_context(ssl_ctx.get(),
						    (const unsigned char *)sid_ctx.data(),
						    sid_ctx.size());
	if (result == 0)
		throw SslError("SSL_CTX_set_session_id_context() failed");
}

UniqueSSL
SslFactory::Make()
{
	UniqueSSL ssl{SSL_new(ssl_ctx.get())};
	if (!ssl)
		throw SslError("SSL_new() failed");

	SSL_set_accept_state(ssl.get());

	return ssl;
}

SslFactoryCertKey::SslFactoryCertKey(const SslCertKeyConfig &config)
{
	auto ck = LoadCertChainKeyFile(config.cert_file.c_str(),
				       config.key_file.c_str());
	cert = std::move(ck.first.front());
	ck.first.pop_front();
	chain = std::move(ck.first);
	key = std::move(ck.second);

	names = GetCertificateNames<Name>(*cert);
}
