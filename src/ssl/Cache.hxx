// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "CAMap.hxx"
#include "NameCache.hxx"
#include "LookupCertResult.hxx"
#include "lib/openssl/UniqueCertKey.hxx"
#include "lib/openssl/IntegralExDataIndex.hxx"
#include "certdb/Config.hxx"
#include "pg/AsyncConnection.hxx"
#include "thread/Notify.hxx"
#include "event/DeferEvent.hxx"
#include "io/Logger.hxx"
#include "util/Cancellable.hxx"
#include "util/IntrusiveHashSet.hxx"
#include "util/IntrusiveList.hxx"
#include "util/TransparentHash.hxx"

#include <unordered_map>
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
	 * Move the StartQuery() call out of the current stack frame.
	 */
	DeferEvent defer_start_query;

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

	CAMap ca_certs;

	/**
	 * Protects #map, #queries, #pending_queries.
	 */
	std::mutex mutex;

	struct Item : UniqueCertKey {
		const std::string special;

		std::chrono::steady_clock::time_point expires;

		Item(UniqueCertKey &&_ck,
		     std::string &&_special,
		     std::chrono::steady_clock::time_point now) noexcept
			:UniqueCertKey(std::move(_ck)),
			 special(std::move(_special)),
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
	std::unordered_multimap<std::string, Item, TransparentHash, std::equal_to<>> map;

	struct Request;
	class Query;

	struct QueryGetKey {
		[[gnu::pure]]
		std::string_view operator()(const Query &query) const noexcept;
	};

	/**
	 * All #Query instances (owned by this container), including
	 * #pending_queries and #current_query.
	 */
	IntrusiveHashSet<Query, 256,
			 IntrusiveHashSetOperators<Query, QueryGetKey, TransparentHash,
						   std::equal_to<std::string_view>>> queries;

	/**
	 * The #queries which have not been started yet, in the order
	 * they were scheduled.
	 */
	IntrusiveList<Query> pending_queries;

	/**
	 * The query that is currently being executed.
	 *
	 * This field is not protected by #mutex because it is
	 * accessed only from the main thread.
	 */
	Query *current_query = nullptr;

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
	LookupCertResult Apply(SSL &ssl, std::string_view host,
			       const char *special) noexcept;

private:
	/**
	 * Add the given certificate/key pair to the cache.
	 *
	 * This method locks the mutex when necessary.
	 */
	UniqueCertKey Add(UniqueCertKey &&ck,
			  std::string &&special);

	/**
	 * Caller must lock #mutex.
	 */
	[[gnu::pure]]
	std::optional<UniqueCertKey> GetNoWildCardCached(std::string_view host,
							 const char *special) noexcept;

	/**
	 * Pop the first uncancelled #Query from #pending_queries
	 * (while holding #mutex).
	 */
	Query *LockPopPendingQuery() noexcept;

	void StartQuery() noexcept;

	void DeferStartQuery() noexcept {
		defer_start_query.Schedule();
	}

	/**
	 * Caller must lock #mutex.
	 */
	void ScheduleQuery(SSL &ssl, std::string_view host,
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
	bool Flush(std::string_view name) noexcept;

	/* virtual methods from Pg::AsyncConnectionHandler */
	void OnConnect() override;
	void OnDisconnect() noexcept override;
	void OnNotify(const char *name) override;
	void OnError(std::exception_ptr e) noexcept override;

	/* virtual methods from class CertNameCacheHandler */
	void OnCertModified(std::string_view name,
			    bool deleted) noexcept override;
};
