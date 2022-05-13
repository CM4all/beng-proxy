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
#include "CompletionHandler.hxx"
#include "lib/openssl/Name.hxx"
#include "lib/openssl/AltName.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/LoadFile.hxx"
#include "certdb/Wildcard.hxx"
#include "certdb/CoCertDatabase.hxx"
#include "co/InvokeTask.hxx"
#include "co/Task.hxx"
#include "event/Loop.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <set>

struct CertCache::Request final : AutoUnlinkIntrusiveListHook, Cancellable {
	SSL &ssl;

	explicit Request(SSL &_ssl) noexcept
		:ssl(_ssl)
	{
		auto &handler = GetSslCompletionHandler(ssl);
		assert(!handler.cancel_ptr);
		handler.cancel_ptr = *this;
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		delete this;
	}
};

class CertCache::Query {
	CertCache &cache;

	std::string host, special;

	IntrusiveList<Request> requests;

	Co::InvokeTask invoke_task;

public:
	template<typename H, typename S>
	Query(CertCache &_cache, H &&_host, S &&_special) noexcept
		:cache(_cache),
		 host(std::forward<H>(_host)),
		 special(std::forward<S>(_special))
	{
	}

	~Query() noexcept {
		assert(requests.empty());
	}

	void AddRequest(Request &request) noexcept {
		requests.push_back(request);
	}

	bool IsRunning() const noexcept {
		return invoke_task;
	}

	bool IsCancelled() const noexcept {
		return requests.empty();
	}

	void Start() noexcept {
		assert(&cache.current_query->second == this);
		assert(!IsRunning());

		invoke_task = Run();
		invoke_task.Start(BIND_THIS_METHOD(OnCompletion));
	}

	/**
	 * Stop execution of the coroutine.  This is only supposed to
	 * be called during shutdown, when it is expected that all
	 * requests will be canceled.
	 */
	void Stop() noexcept {
		assert(&cache.current_query->second == this);
		assert(IsRunning());

		invoke_task = {};
	}

private:
	Co::InvokeTask Run();

	void OnCompletion(std::exception_ptr error) noexcept;
};

static Co::Task<UniqueCertKey>
CoGetServerCertificateKeyMaybeWildcard(Pg::AsyncConnection &connection,
				       const CertDatabaseConfig &config,
				       const char *name, const char *special)
{
	auto cert_key = co_await CoGetServerCertificateKey(connection, config,
							   name, special);
	if (!cert_key) {
		const auto wildcard = MakeCommonNameWildcard(name);
		if (!wildcard.empty())
			cert_key = co_await
				CoGetServerCertificateKey(connection, config,
							  wildcard.c_str(),
							  special);
	}

	co_return cert_key;
}

Co::InvokeTask
CertCache::Query::Run()
{
	assert(&cache.current_query->second == this);

	const char *_special = special.empty() ? nullptr : special.c_str();

	auto cert_key = co_await CoGetServerCertificateKeyMaybeWildcard(cache.db,
									cache.config,
									host.c_str(),
									_special);
	if (!cert_key)
		/* certificate was not found; the
		   SslCompletionHandlers will be invoked by
		   OnCompletion() */
		co_return;

	cert_key = cache.Add(std::move(cert_key),
			     _special);

	requests.clear_and_dispose([this, &cert_key](Request *request){
		try {
			cache.Apply(request->ssl, cert_key);
			cache.state_idx.Set(request->ssl, State::COMPLETE);
		} catch (...) {
			cache.logger(1, std::current_exception());
			cache.state_idx.Set(request->ssl, State::ERROR);
		}

		InvokeSslCompletionHandler(request->ssl);
		delete request;
	});
}

inline void
CertCache::Query::OnCompletion(std::exception_ptr error) noexcept
{
	assert(&cache.current_query->second == this);

	const State new_state = error ? State::ERROR : State::NOT_FOUND;

	if (error)
		cache.logger(1, error);

	/* invoke all remaining SslCompletionHandlers; this is
	   only relevant if Run() has not finished
	   sucessfully */
	requests.clear_and_dispose([this, new_state](Request *request){
		cache.state_idx.Set(request->ssl, new_state);
		InvokeSslCompletionHandler(request->ssl);
		delete request;
	});

	/* copy the "cache" reference to the stack because the
	   following erase() call will delete this object */
	auto &_cache = cache;

	{
		const std::unique_lock<std::mutex> lock{_cache.mutex};
		_cache.queries.erase(_cache.current_query);
	}

	_cache.current_query = _cache.queries.end();

	/* start the next query */
	_cache.StartQuery();
}

CertCache::CertCache(EventLoop &event_loop,
		     const CertDatabaseConfig &_config) noexcept
	:logger("CertCache"), config(_config),
	 query_added_notify(event_loop, BIND_THIS_METHOD(StartQuery)),
	 db(event_loop, config.connect.c_str(), config.schema.c_str(),
	    *this),
	 name_cache(event_loop, _config, *this)
{
}

CertCache::~CertCache() noexcept = default;

void
CertCache::Expire() noexcept
{
	const auto now = GetEventLoop().SteadyNow();

	const std::unique_lock<std::mutex> lock(mutex);
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

void
CertCache::Connect() noexcept
{
	db.Connect();
	name_cache.Connect();
}

void
CertCache::Disconnect() noexcept
{
	name_cache.Disconnect();

	if (current_query != queries.end()) {
		const std::unique_lock lock{mutex};
		current_query->second.Stop();
		current_query = queries.end();
	}

	db.Disconnect();
	query_added_notify.Disable();
}

inline UniqueCertKey
CertCache::Add(UniqueCertKey &&ck, const char *special)
{
	assert(ck);

	ERR_clear_error();

	const auto name = GetCommonName(*ck.cert);
	if (name == nullptr)
		throw std::runtime_error("Certificate without common name");

	const std::unique_lock<std::mutex> lock(mutex);
	auto i = map.emplace(std::piecewise_construct,
			     std::forward_as_tuple(name.c_str()),
			     std::forward_as_tuple(std::move(ck),
						   GetEventLoop().SteadyNow()));

	if (special != nullptr)
		i->second.special = special;

	/* create shadow items for all altNames */
	std::set<std::string> alt_names;
	for (auto &a : GetSubjectAltNames(*i->second.cert))
		alt_names.emplace(std::move(a));

	alt_names.erase(i->first);

	for (auto &a : alt_names)
		map.emplace(std::move(a), i->second);

	return UpRef(i->second);
}

inline std::optional<UniqueCertKey>
CertCache::GetNoWildCardCached(const char *host, const char *_special) noexcept
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
			return UpRef(i->second);
		}
	}

	return std::nullopt;
}

void
CertCache::StartQuery() noexcept
{
	if (current_query != queries.end())
		/* already busy */
		return;

	if (!db.IsReady())
		/* database is (re)connecting */
		return;

	/* pick an arbitrary request and start the database query */
	const std::unique_lock<std::mutex> lock{mutex};
	while (!queries.empty()) {
		auto i = queries.begin();
		if (!i->second.IsCancelled()) {
			/* found a candidate - start it */
			current_query = i;
			current_query->second.Start();
			break;
		}

		/* this query was scheduled, but meanwhile all
		   requests were cancelled, so don't bother */
		queries.erase(i);
	}
}

void
CertCache::ScheduleQuery(SSL &ssl, const char *host,
			 const char *special) noexcept
{
	std::string key(host);
	if (special != nullptr) {
		key.push_back(0);
		key.append(special);
	}

	bool was_empty;

	{
		const std::unique_lock<std::mutex> lock{mutex};
		was_empty = queries.empty();

		const char *_special = special != nullptr ? special : "";
		auto &query = queries.try_emplace(std::move(key),
						  *this, host, _special).first->second;

		auto *request = new Request(ssl);
		query.AddRequest(*request);
	}

	if (was_empty)
		query_added_notify.Signal();
}

inline void
CertCache::Apply(SSL &ssl, X509 &cert, EVP_PKEY &key)
{
	ERR_clear_error();

	if (SSL_use_PrivateKey(&ssl, &key) != 1)
		throw SslError("SSL_use_PrivateKey() failed");

	if (SSL_use_certificate(&ssl, &cert) != 1)
		throw SslError("SSL_use_certificate() failed");

	if (X509_NAME *issuer = X509_get_issuer_name(&cert);
	    issuer != nullptr) {
		auto i = ca_certs.find(CalcSHA1(*issuer));
		if (i != ca_certs.end())
			for (const auto &ca_cert : i->second)
				SSL_add1_chain_cert(&ssl, ca_cert.get());
	}
}

inline void
CertCache::Apply(SSL &ssl, const UniqueCertKey &cert_key)
{
	Apply(ssl, *cert_key.cert, *cert_key.key);
}

inline LookupCertResult
CertCache::ApplyAndSetState(SSL &ssl, const UniqueCertKey &cert_key) noexcept
{
	try {
		Apply(ssl, cert_key);
		state_idx.Set(ssl, State::COMPLETE);
		return LookupCertResult::COMPLETE;
	} catch (...) {
		logger(1, std::current_exception());
		state_idx.Set(ssl, State::ERROR);
		return LookupCertResult::ERROR;
	}
}

LookupCertResult
CertCache::Apply(SSL &ssl, const char *host,
		 const char *special) noexcept
{
	assert(db.IsDefined());

	switch (state_idx.Get(ssl)) {
	case State::NONE:
		break;

	case State::IN_PROGRESS:
		/* registered again, already in progress */
		return LookupCertResult::IN_PROGRESS;

	case State::COMPLETE:
		/* registered again, but was already found */
		return LookupCertResult::COMPLETE;

	case State::NOT_FOUND:
		/* registered again, but was not found */
		return LookupCertResult::NOT_FOUND;

	case State::ERROR:
		return LookupCertResult::ERROR;
	}

	if (auto item = GetNoWildCardCached(host, special))
		return ApplyAndSetState(ssl, *item);

	const auto wildcard = MakeCommonNameWildcard(host);
	if (!wildcard.empty()) {
		if (auto item = GetNoWildCardCached(wildcard.c_str(), special))
			return ApplyAndSetState(ssl, *item);
	}

	if (name_cache.Lookup(host) ||
	    (!wildcard.empty() && name_cache.Lookup(wildcard.c_str()))) {
		state_idx.Set(ssl, State::IN_PROGRESS);
		ScheduleQuery(ssl, host, special);
		return LookupCertResult::IN_PROGRESS;
	}

	state_idx.Set(ssl, State::NOT_FOUND);
	return LookupCertResult::NOT_FOUND;
}

bool
CertCache::Flush(const std::string &name) noexcept
{
	auto r = map.equal_range(name);
	if (r.first == r.second)
		return false;

	std::set<std::string> alt_names;

	for (auto i = r.first; i != r.second;) {
		const auto &item = i->second;

		/* if this is a primary item (not a shadow item for an
		   altName), collect all altNames to be flushed
		   later */
		if (name == GetCommonName(*item.cert).c_str())
			for (auto &a : GetSubjectAltNames(*item.cert))
				alt_names.emplace(std::move(a));

		i = map.erase(i);
	}

	/* now flush all altNames */
	for (const auto &i : alt_names)
		Flush(i);

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

void
CertCache::OnConnect()
{
	logger(5, "connected to certificate database");

	StartQuery();
}

void
CertCache::OnDisconnect() noexcept
{
	logger(4, "disconnected from certificate database");
}

void
CertCache::OnNotify(const char *name)
{
	logger(5, "received notify '", name, "'");
}

void
CertCache::OnError(std::exception_ptr e) noexcept
{
	logger(1, e);
}
