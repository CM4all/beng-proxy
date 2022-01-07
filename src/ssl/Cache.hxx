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

#pragma once

#include "NameCache.hxx"
#include "lib/openssl/Hash.hxx"
#include "lib/openssl/UniqueX509.hxx"
#include "lib/openssl/UniqueEVP.hxx"
#include "certdb/Config.hxx"
#include "certdb/CertDatabase.hxx"
#include "stock/ThreadedStock.hxx"
#include "io/Logger.hxx"

#include <unordered_map>
#include <map>
#include <forward_list>
#include <string>
#include <mutex>
#include <chrono>

#include <string.h>

class CertDatabase;

/**
 * A frontend for #CertDatabase which caches results as SSL_CTX
 * instance.  It is thread-safe, designed to be called synchronously
 * by worker threads (via #SslFilter).
 */
class CertCache final : CertNameCacheHandler {
	const LLogger logger;

	const CertDatabaseConfig config;

	CertNameCache name_cache;

	struct SHA1Compare {
		[[gnu::pure]]
		bool operator()(const SHA1Digest &a,
				const SHA1Digest &b) const noexcept {
			return memcmp(&a, &b, sizeof(a)) < 0;
		}
	};

	std::map<SHA1Digest, std::forward_list<UniqueX509>, SHA1Compare> ca_certs;

	/**
	 * Database connections used by worker threads.
	 */
	ThreadedStock<CertDatabase> dbs;

	std::mutex mutex;

	struct Item {
		const UniqueX509 cert;
		const UniqueEVP_PKEY key;

		std::string special;

		std::chrono::steady_clock::time_point expires;

		template<typename C, typename K>
		Item(C &&_cert, K &&_key,
		     std::chrono::steady_clock::time_point now) noexcept
			:cert(std::forward<C>(_cert)),
			 key(std::forward<K>(_key)),
			 /* the initial expiration is 6 hours; it will be raised
			    to 24 hours if the certificate is used again */
			 expires(now + std::chrono::hours(6)) {}

		Item(const Item &src) noexcept
			:cert(src.cert.get()),
			 key(src.key.get()),
			 special(src.special),
			 expires(src.expires) {
			// TODO: this should be part of UniqueX509/UniqueEVP_PKEY
			X509_up_ref(cert.get());
			EVP_PKEY_up_ref(key.get());
		}
	};

	/**
	 * Map host names to SSL_CTX instances.  The key may be a
	 * wildcard.
	 */
	std::unordered_multimap<std::string, Item> map;

public:
	explicit CertCache(EventLoop &event_loop,
			   const CertDatabaseConfig &_config) noexcept
		:logger("CertCache"), config(_config),
		 name_cache(event_loop, _config, *this) {}

	auto &GetEventLoop() const noexcept {
		return name_cache.GetEventLoop();
	}

	void LoadCaCertificate(const char *path);

	void Connect() noexcept {
		name_cache.Connect();
	}

	void Disconnect() noexcept {
		name_cache.Disconnect();
	}

	void Expire() noexcept;

	/**
	 * Look up a certificate by host name, and set it in the given
	 * #SSL.  Returns true on success, false if a certificate for
	 * that name was not found, and throws an exception on error.
	 */
	bool Apply(SSL &ssl, const char *host, const char *special);

private:
	const Item &Add(UniqueX509 &&cert, UniqueEVP_PKEY &&key,
			const char *special);
	const Item *Query(const char *host, const char *special);

	[[gnu::pure]]
	const Item *GetCached(const char *host, const char *special) noexcept;

	const Item *GetNoWildCard(const char *host, const char *special);
	const Item *Get(const char *host, const char *special);

	/**
	 * Flush items with the given name.
	 *
	 * Caller must lock the mutex.
	 *
	 * @return true if at least one item was found and deleted
	 */
	bool Flush(const std::string &name) noexcept;

	/* virtual methods from class CertNameCacheHandler */
	void OnCertModified(const std::string &name,
			    bool deleted) noexcept override;
};
