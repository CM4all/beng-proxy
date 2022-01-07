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

#include "Cache.hxx"
#include "SessionCache.hxx"
#include "lib/openssl/Name.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/LoadFile.hxx"
#include "certdb/Wildcard.hxx"
#include "event/Loop.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/err.h>

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

inline const CertCache::Item &
CertCache::Add(UniqueX509 &&cert, UniqueEVP_PKEY &&key, const char *special)
{
	assert(cert);
	assert(key);

	ERR_clear_error();

	const auto name = GetCommonName(*cert);
	if (name == nullptr)
		throw std::runtime_error("Certificate without common name");

	const std::unique_lock<std::mutex> lock(mutex);
	auto i = map.emplace(std::piecewise_construct,
			     std::forward_as_tuple(name.c_str()),
			     std::forward_as_tuple(std::move(cert),
						   std::move(key),
						   GetEventLoop().SteadyNow()));

	if (special != nullptr)
		i->second.special = special;

	return i->second;
}

const CertCache::Item *
CertCache::Query(const char *host, const char *special)
{
	auto db = dbs.Get(config);
	db->EnsureConnected();

	auto cert_key = db->GetServerCertificateKey(host, special);
	if (!cert_key.second)
		return nullptr;

	return &Add(std::move(cert_key.first), std::move(cert_key.second),
		    special);
}

inline const CertCache::Item *
CertCache::GetCached(const char *host, const char *_special) noexcept
{
	const std::unique_lock<std::mutex> lock{mutex};

	const std::string_view special{
		_special != nullptr
		? std::string_view{_special}
		: std::string_view{}
	};

	for (auto [i, end] = map.equal_range(host); i != end; ++i) {
		if (i->second.special == special) {
			i->second.expires = GetEventLoop().SteadyNow() + std::chrono::hours(24);
			return &i->second;
		}
	}

	return nullptr;
}

const CertCache::Item *
CertCache::GetNoWildCard(const char *host, const char *special)
{
	if (const auto *item = GetCached(host, special))
		return item;

	if (name_cache.Lookup(host)) {
		if (const auto *item = Query(host, special))
			return item;
	}

	return nullptr;
}

const CertCache::Item *
CertCache::Get(const char *host, const char *special)
{
	const auto *item = GetNoWildCard(host, special);
	if (item == nullptr) {
		/* not found: try the wildcard */
		const auto wildcard = MakeCommonNameWildcard(host);
		if (!wildcard.empty())
			item = GetNoWildCard(wildcard.c_str(), special);
	}

	return item;
}

bool
CertCache::Apply(SSL &ssl, const char *host, const char *special)
{
	const auto *item = Get(host, special);
	if (item == nullptr)
		return false;

	ERR_clear_error();

	if (SSL_use_PrivateKey(&ssl, item->key.get()) != 1)
		throw SslError("SSL_use_PrivateKey() failed");

	if (SSL_use_certificate(&ssl, item->cert.get()) != 1)
		throw SslError("SSL_use_certificate() failed");

	if (X509_NAME *issuer = X509_get_issuer_name(item->cert.get());
	    issuer != nullptr) {
		auto i = ca_certs.find(CalcSHA1(*issuer));
		if (i != ca_certs.end())
			for (const auto &ca_cert : i->second)
				SSL_add1_chain_cert(&ssl, ca_cert.get());
	}

	return true;
}

bool
CertCache::Flush(const std::string &name) noexcept
{
	auto r = map.equal_range(name);
	if (r.first == r.second)
		return false;

	map.erase(r.first, r.second);
	return true;
}

void
CertCache::OnCertModified(const std::string &name, bool deleted) noexcept
{
	const std::unique_lock<std::mutex> lock(mutex);

	if (Flush(name))
		logger.Format(5, "flushed %s certificate '%s'",
			      deleted ? "deleted" : "modified",
			      name.c_str());
}
