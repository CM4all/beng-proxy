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

#include "Factory.hxx"
#include "Basic.hxx"
#include "Config.hxx"
#include "SessionCache.hxx"
#include "SniCallback.hxx"
#include "AlpnEnable.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/Ctx.hxx"
#include "lib/openssl/Name.hxx"
#include "lib/openssl/AltName.hxx"
#include "lib/openssl/Key.hxx"
#include "util/AllocatedString.hxx"
#include "util/ConstBuffer.hxx"
#include "util/StringView.hxx"
#include "util/PrintException.hxx"

#include <openssl/ssl.h>
#include <openssl/err.h>

#include <algorithm>
#include <forward_list>

#include <assert.h>

struct SslFactoryCertKey {
	struct Name {
		AllocatedString value;
		size_t length;

		Name(AllocatedString &&_value)
			:value(std::move(_value)), length(strlen(value.c_str())) {}

		Name(std::string_view _value)
			:value(_value),
			 length(_value.size()) {}

		[[gnu::pure]]
		bool Match(StringView host_name) const;
	};

	SslCtx ssl_ctx;

	std::forward_list<Name> names;

	SslFactoryCertKey() = default;

	SslFactoryCertKey(SslFactoryCertKey &&other) = default;
	SslFactoryCertKey &operator=(SslFactoryCertKey &&other) = default;

	void LoadServer(const SslConfig &parent_config,
			const SslCertKeyConfig &config);

	void CacheCommonName(X509_NAME &subject) {
		auto common_name = NidToString(subject, NID_commonName);
		if (common_name != nullptr)
			names.emplace_front(std::move(common_name));
	}

	void CacheCommonName(X509 &cert) {
		X509_NAME *subject = X509_get_subject_name(&cert);
		if (subject != nullptr)
			CacheCommonName(*subject);

		for (const auto &i : GetSubjectAltNames(cert))
			names.emplace_front(i.c_str());
	}

	[[gnu::pure]]
	bool MatchCommonName(StringView host_name) const;

	void SetSessionIdContext(ConstBuffer<void> _sid_ctx) {
		auto sid_ctx = ConstBuffer<unsigned char>::FromVoid(_sid_ctx);
		int result = SSL_CTX_set_session_id_context(ssl_ctx.get(),
							    sid_ctx.data,
							    sid_ctx.size);
		if (result == 0)
			throw SslError("SSL_CTX_set_session_id_context() failed");
	}

	void EnableAlpnH2() noexcept {
		::EnableAlpnH2(*ssl_ctx);
	}

	UniqueSSL Make() const {
		UniqueSSL ssl(SSL_new(ssl_ctx.get()));
		if (!ssl)
			throw SslError("SSL_new() failed");

		return ssl;
	}

	void Apply(SSL *ssl) const {
		SSL_set_SSL_CTX(ssl, ssl_ctx.get());
	}

	unsigned Flush(long tm) {
		return ::FlushSessionCache(*ssl_ctx, tm);
	}
};

void
SslFactory::LoadCertsKeys(const SslConfig &config)
{
	cert_key.reserve(config.cert_key.size());

	for (const auto &c : config.cert_key) {
		SslFactoryCertKey ck;
		ck.LoadServer(config, c);

		cert_key.emplace_back(std::move(ck));
	}
}

static void
ApplyServerConfig(SSL_CTX *ssl_ctx, const SslCertKeyConfig &cert_key)
{
	ERR_clear_error();

	if (SSL_CTX_use_PrivateKey_file(ssl_ctx,
					cert_key.key_file.c_str(),
					SSL_FILETYPE_PEM) != 1)
		throw SslError("Failed to load key file " +
			       cert_key.key_file);

	if (SSL_CTX_use_certificate_chain_file(ssl_ctx,
					       cert_key.cert_file.c_str()) != 1)
		throw SslError("Failed to load certificate file " +
			       cert_key.cert_file);

	if (SSL_CTX_check_private_key(ssl_ctx) != 1)
		throw SslError("Key '" + cert_key.key_file +
			       "' does not match certificate '" +
			       cert_key.cert_file + "'");
}

inline bool
SslFactoryCertKey::Name::Match(StringView host_name) const
{
	if (value == nullptr)
		return false;

	if (length == host_name.size &&
	    memcmp(host_name.data, value.c_str(), host_name.size) == 0)
		return true;

	if (value[0] == '*' && value[1] == '.' && value[2] != 0) {
		if (host_name.size >= length &&
		    /* match only one segment (no dots) */
		    memchr(host_name.data, '.',
			   host_name.size - length + 1) == nullptr &&
		    memcmp(host_name.data + host_name.size - length + 1,
			   value.c_str() + 1, length - 1) == 0)
			return true;
	}

	return false;
}

inline bool
SslFactoryCertKey::MatchCommonName(StringView host_name) const
{
	for (const auto &name : names)
		if (name.Match(host_name))
			return true;

	return false;
}

inline
SslFactory::SslFactory(std::unique_ptr<SslSniCallback> &&_sni) noexcept
	:sni(std::move(_sni))
{
}

SslFactory::~SslFactory() noexcept = default;

inline const SslFactoryCertKey *
SslFactory::FindCommonName(StringView host_name) const
{
	for (const auto &ck : cert_key)
		if (ck.MatchCommonName(host_name))
			return &ck;

	return nullptr;
}

static int
ssl_servername_callback(SSL *ssl, int *,
			const SslFactory &factory)
{
	const char *_host_name = SSL_get_servername(ssl, TLSEXT_NAMETYPE_host_name);
	if (_host_name == nullptr)
		return SSL_TLSEXT_ERR_OK;

	const StringView host_name(_host_name);

	/* find the first certificate that matches */

	const auto *ck = factory.FindCommonName(host_name);
	if (ck != nullptr) {
		/* found it - now use it */
		ck->Apply(ssl);
	} else if (factory.GetSNI()) {
		try {
			factory.GetSNI()->OnSni(ssl, host_name.data);
		} catch (const std::exception &e) {
			PrintException(e);
		}
	}

	return SSL_TLSEXT_ERR_OK;
}

inline void
SslFactory::EnableSNI()
{
	SSL_CTX *ssl_ctx = cert_key.front().ssl_ctx.get();

	if (!SSL_CTX_set_tlsext_servername_callback(ssl_ctx,
						    ssl_servername_callback) ||
	    !SSL_CTX_set_tlsext_servername_arg(ssl_ctx, this))
		throw SslError("SSL_CTX_set_tlsext_servername_callback() failed");
}

inline void
SslFactory::AutoEnableSNI()
{
	if (cert_key.size() > 1 || sni)
		EnableSNI();
}

void
SslFactory::SetSessionIdContext(ConstBuffer<void> sid_ctx)
{
	for (auto &i : cert_key)
		i.SetSessionIdContext(sid_ctx);
}

void
SslFactory::EnableAlpnH2()
{
	for (auto &i : cert_key)
		i.EnableAlpnH2();
}

UniqueSSL
SslFactory::Make()
{
	auto ssl = cert_key.front().Make();

	SSL_set_accept_state(ssl.get());

	return ssl;
}

unsigned
SslFactory::Flush(long tm)
{
	unsigned n = 0;
	for (auto &i : cert_key)
		n += i.Flush(tm);
	return n;
}

void
SslFactoryCertKey::LoadServer(const SslConfig &parent_config,
			      const SslCertKeyConfig &config)
{
	assert(!ssl_ctx);

	ssl_ctx = CreateBasicSslCtx(true);

	assert(!parent_config.cert_key.empty());

	ApplyServerConfig(ssl_ctx.get(), config);
	ApplyServerConfig(*ssl_ctx, parent_config);

	auto ssl = Make();

	X509 *cert = SSL_get_certificate(ssl.get());
	if (cert == nullptr)
		throw SslError("No certificate in SSL_CTX");

	EVP_PKEY *key = SSL_get_privatekey(ssl.get());
	if (key == nullptr)
		throw SslError("No certificate in SSL_CTX");

	if (!MatchModulus(*cert, *key))
		/* this shouldn't ever fail because
		   SSL_CTX_check_private_key() was successful */
		throw SslError("Key '" + config.key_file +
			       "' does not match certificate '" +
			       config.cert_file + "'");

	CacheCommonName(*cert);
}

std::unique_ptr<SslFactory>
ssl_factory_new_server(const SslConfig &config,
		       std::unique_ptr<SslSniCallback> &&sni)
{
	assert(!config.cert_key.empty());

	std::unique_ptr<SslFactory> factory(new SslFactory(std::move(sni)));

	factory->LoadCertsKeys(config);
	factory->AutoEnableSNI();

	return factory;
}
