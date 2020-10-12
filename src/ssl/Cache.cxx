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

#include "Cache.hxx"
#include "SessionCache.hxx"
#include "Basic.hxx"
#include "AlpnEnable.hxx"
#include "ssl/Name.hxx"
#include "ssl/Error.hxx"
#include "ssl/LoadFile.hxx"
#include "certdb/Wildcard.hxx"
#include "event/Loop.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/err.h>

unsigned
CertCache::FlushSessionCache(long tm) noexcept
{
	unsigned n = 0;

	for (auto &i : map)
		n += ::FlushSessionCache(*i.second.ssl_ctx, tm);

	return n;
}

void
CertCache::Expire() noexcept
{
	const auto now = GetEventLoop().SteadyNow();

	for (auto i = map.begin(), end = map.end(); i != end;) {
		if (now >= i->second.expires) {
			logger(5, "flushed certificate '", i->first, "'");
			i = map.erase(i);
		} else
			++i;
	}
}

void
CertCache::LoadCaCertificate(const char *path)
{
	auto chain = LoadCertChainFile(path);
	assert(!chain.empty());

	X509_NAME *subject = X509_get_subject_name(chain.front().get());
	if (subject == nullptr)
		throw SslError(std::string("CA certificate has no subject: ") + path);

	auto digest = CalcSHA1(*subject);
	auto r = ca_certs.emplace(std::move(digest), std::move(chain));
	if (!r.second)
		throw SslError(std::string("Duplicate CA certificate: ") + path);
}

SslCtx
CertCache::Add(UniqueX509 &&cert, UniqueEVP_PKEY &&key, bool alpn_h2)
{
	assert(cert);
	assert(key);

	auto ssl_ctx = CreateBasicSslCtx(true);
	// TODO: call ApplyServerConfig()

	if (alpn_h2)
		EnableAlpnH2(*ssl_ctx);

	ERR_clear_error();

	const auto name = GetCommonName(*cert);

	X509_NAME *issuer = X509_get_issuer_name(cert.get());

	if (SSL_CTX_use_PrivateKey(ssl_ctx.get(), key.get()) != 1)
		throw SslError("SSL_CTX_use_PrivateKey() failed");

	if (SSL_CTX_use_certificate(ssl_ctx.get(), cert.get()) != 1)
		throw SslError("SSL_CTX_use_certificate() failed");

	if (issuer != nullptr) {
		auto i = ca_certs.find(CalcSHA1(*issuer));
		if (i != ca_certs.end())
			for (const auto &ca_cert : i->second)
				SSL_CTX_add_extra_chain_cert(ssl_ctx.get(),
							     X509_dup(ca_cert.get()));
	}

	if (name != nullptr) {
		const std::unique_lock<std::mutex> lock(mutex);
		map.emplace(std::piecewise_construct,
			    std::forward_as_tuple(MakeCacheKey(name.c_str(),
							       alpn_h2)),
			    std::forward_as_tuple(ssl_ctx,
						  GetEventLoop().SteadyNow()));
	}

	return ssl_ctx;
}

SslCtx
CertCache::Query(const char *host, bool alpn_h2)
{
	auto db = dbs.Get(config);
	db->EnsureConnected();

	auto cert_key = db->GetServerCertificateKey(host);
	if (!cert_key.second)
		return SslCtx();

	return Add(std::move(cert_key.first), std::move(cert_key.second),
		   alpn_h2);
}

SslCtx
CertCache::GetNoWildCard(const char *host, bool alpn_h2)
{
	{
		const std::unique_lock<std::mutex> lock(mutex);
		auto i = map.find(MakeCacheKey(host, alpn_h2));
		if (i != map.end()) {
			i->second.expires = GetEventLoop().SteadyNow() + std::chrono::hours(24);
			return i->second.ssl_ctx;
		}
	}

	if (name_cache.Lookup(host)) {
		auto ssl_ctx = Query(host, alpn_h2);
		if (ssl_ctx)
			return ssl_ctx;
	}

	return {};
}

SslCtx
CertCache::Get(const char *host, bool alpn_h2)
{
	auto ssl_ctx = GetNoWildCard(host, alpn_h2);
	if (!ssl_ctx) {
		/* not found: try the wildcard */
		const auto wildcard = MakeCommonNameWildcard(host);
		if (!wildcard.empty())
			ssl_ctx = GetNoWildCard(wildcard.c_str(), alpn_h2);
	}

	return ssl_ctx;
}

void
CertCache::OnCertModified(const std::string &name, bool deleted) noexcept
{
	const std::unique_lock<std::mutex> lock(mutex);
	auto i = map.find(name);
	if (i != map.end()) {
		map.erase(i);

		logger.Format(5, "flushed %s certificate '%s'",
			      deleted ? "deleted" : "modified",
			      name.c_str());
	}

	i = map.find(MakeCacheKey(name, true));
	if (i != map.end()) {
		map.erase(i);

		logger.Format(5, "flushed %s certificate '%s'",
			      deleted ? "deleted" : "modified",
			      name.c_str());
	}
}
