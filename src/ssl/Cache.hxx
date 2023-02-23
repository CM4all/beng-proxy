// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "NameCache.hxx"
#include "LookupCertResult.hxx"
#include "lib/openssl/Hash.hxx"
#include "lib/openssl/UniqueX509.hxx"
#include "lib/openssl/UniqueCertKey.hxx"
#include "lib/openssl/IntegralExDataIndex.hxx"
#include "certdb/Config.hxx"
#include "pg/AsyncConnection.hxx"
#include "thread/Notify.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/IntrusiveList.hxx"

#include <unordered_map>
#include <map>
#include <forward_list>
#include <string>
#include <mutex>
#include <chrono>
#include <optional>

#include <string.h>

class CertDatabase;

/**
 * A frontend for #CertDatabase which caches results as SSL_CTX
 * instance.  It is thread-safe, designed to be called synchronously
 * by worker threads (via #SslFilter).
 */
class CertCache final : Pg::AsyncConnectionHandler, CertNameCacheHandler {
	const LLogger logger;

	const CertDatabaseConfig config;

	enum class State {
		NONE = 0,
		IN_PROGRESS,
		COMPLETE,
		NOT_FOUND,
		ERROR,
	};

	const OpenSSL::IntegralExDataIndex<State> state_idx;

	/**
	 * Used to move the Apply() call from a worker thread to the
	 * main thread.  The worker thread adds a #Request/#Query to
	 * the #queries map and then signals this object, which
	 * triggers a StartQuery() call in the main thread.
	 */
	Notify query_added_notify;

	Pg::AsyncConnection db;

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
	 * Protects #map, #queries.
	 */
	std::mutex mutex;

	struct Item : UniqueCertKey {
		std::string special;

		std::chrono::steady_clock::time_point expires;

		Item(UniqueCertKey &&_ck,
		     std::chrono::steady_clock::time_point now) noexcept
			:UniqueCertKey(std::move(_ck)),
			 /* the initial expiration is 6 hours; it will be raised
			    to 24 hours if the certificate is used again */
			 expires(now + std::chrono::hours(6)) {}

		Item(const Item &src) noexcept
			:UniqueCertKey(UpRef(src)),
			 special(src.special),
			 expires(src.expires) {}
	};

	/**
	 * Map host names to SSL_CTX instances.  The key may be a
	 * wildcard.
	 */
	std::unordered_multimap<std::string, Item> map;

	struct Request;
	class Query;

	using QueryMap = std::map<std::string, Query>;
	QueryMap queries;

	/**
	 * The query that is currently being executed.
	 *
	 * This field is not protected by #mutex because it is
	 * accessed only from the main thread.
	 */
	QueryMap::iterator current_query = queries.end();

public:
	CertCache(EventLoop &event_loop,
		  const CertDatabaseConfig &_config) noexcept;

	~CertCache() noexcept;

	auto &GetEventLoop() const noexcept {
		return name_cache.GetEventLoop();
	}

	void LoadCaCertificate(const char *path);

	void Connect() noexcept;
	void Disconnect() noexcept;

	void Expire() noexcept;

	/**
	 * Look up a certificate by host name, and set it in the given
	 * #SSL.
	 *
	 * @param ssl a #SSL object which must have a
	 * #SslCompletionHandler (via SetSslCompletionHandler()); this
	 * handler will be invoked after this method has returned
	 * #IN_PROGRESS; using its #CancellablePointer field, the
	 * caller may cancel the operation
	 */
	LookupCertResult Apply(SSL &ssl, const char *host,
			       const char *special) noexcept;

private:
	/**
	 * Add the given certificate/key pair to the cache.
	 *
	 * This method locks the mutex when necessary.
	 */
	UniqueCertKey Add(UniqueCertKey &&ck,
			  const char *special);

	[[gnu::pure]]
	std::optional<UniqueCertKey> GetNoWildCardCached(const char *host,
							 const char *special) noexcept;

	void StartQuery() noexcept;

	void ScheduleQuery(SSL &ssl, const char *host,
			   const char *special) noexcept;

	void Apply(SSL &ssl, X509 &cert, EVP_PKEY &key);
	void Apply(SSL &ssl, const UniqueCertKey &cert_key);

	LookupCertResult ApplyAndSetState(SSL &ssl,
					  const UniqueCertKey &cert_key) noexcept;

	/**
	 * Flush items with the given name.
	 *
	 * Caller must lock the mutex.
	 *
	 * @return true if at least one item was found and deleted
	 */
	bool Flush(const std::string &name) noexcept;

	/* virtual methods from Pg::AsyncConnectionHandler */
	void OnConnect() override;
	void OnDisconnect() noexcept override;
	void OnNotify(const char *name) override;
	void OnError(std::exception_ptr e) noexcept override;

	/* virtual methods from class CertNameCacheHandler */
	void OnCertModified(const std::string &name,
			    bool deleted) noexcept override;
};
