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

#include "Progress.hxx"
#include "AcmeUtil.hxx"
#include "AcmeError.hxx"
#include "AcmeClient.hxx"
#include "AcmeOrder.hxx"
#include "AcmeAuthorization.hxx"
#include "AcmeChallenge.hxx"
#include "AcmeKey.hxx"
#include "AcmeHttp.hxx"
#include "AcmeDns.hxx"
#include "AcmeConfig.hxx"
#include "Config.hxx"
#include "CertDatabase.hxx"
#include "WrapKey.hxx"
#include "Wildcard.hxx"
#include "ssl/Init.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Dummy.hxx"
#include "ssl/Edit.hxx"
#include "ssl/Key.hxx"
#include "ssl/LoadFile.hxx"
#include "ssl/AltName.hxx"
#include "ssl/Name.hxx"
#include "ssl/GeneralName.hxx"
#include "ssl/MemBio.hxx"
#include "ssl/Unique.hxx"
#include "ssl/Error.hxx"
#include "lb/Config.hxx"
#include "system/Urandom.hxx"
#include "system/Error.hxx"
#include "io/StringFile.hxx"
#include "util/ConstBuffer.hxx"
#include "util/PrintException.hxx"
#include "util/RuntimeError.hxx"
#include "util/Compiler.h"

#include <map>
#include <memory>
#include <thread>
#include <stdexcept>
#include <set>
#include <variant>

#include <stdio.h>
#include <stdlib.h>

struct Usage {
	const char *text;

	explicit Usage(const char *_text):text(_text) {}
};

struct AutoUsage {};

static WorkshopProgress root_progress;

static CertDatabaseConfig
LoadCertDatabaseConfig(const char *path)
{
	LbConfig lb_config;
	LoadConfigFile(lb_config, path);

	auto i = lb_config.cert_dbs.begin();
	if (i == lb_config.cert_dbs.end())
		throw "/etc/cm4all/beng/lb.conf contains no cert_db section";

	if (std::next(i) != lb_config.cert_dbs.end())
		fprintf(stderr, "Warning: %s contains multiple cert_db sections\n",
			path);

	return std::move(i->second);
}

static CertDatabaseConfig
LoadCertDatabaseConfig()
{
	return LoadCertDatabaseConfig("/etc/cm4all/beng/lb.conf");
}

/**
 * Load the "cert_db" section from "/etc/cm4all/beng/lb.conf", and
 * allow overriding the "connect" value from
 * "/etc/cm4all/beng/certdb.connect".
 */
static CertDatabaseConfig
LoadPatchCertDatabaseConfig()
{
	CertDatabaseConfig config = LoadCertDatabaseConfig();

	try {
		config.connect = LoadStringFile("/etc/cm4all/beng/certdb.connect");
	} catch (const std::system_error &e) {
		/* ignore ENOENT */
		if (!IsFileNotFound(e))
			throw;
	}

	return config;
}

static void
LoadCertificate(const CertDatabaseConfig &db_config,
		const char *handle,
		const char *cert_path, const char *key_path)
{
	const ScopeSslGlobalInit ssl_init;

	const auto cert = LoadCertFile(cert_path);
	const auto common_name = GetCommonName(*cert);
	if (common_name == nullptr)
		throw "Certificate has no common name";

	const auto key = LoadKeyFile(key_path);
	if (!MatchModulus(*cert, *key))
		throw "Key and certificate do not match.";

	WrapKeyHelper wrap_key_helper;
	const auto wrap_key = wrap_key_helper.SetEncryptKey(db_config);

	CertDatabase db(db_config);

	bool inserted;

	db.DoSerializableRepeat(8, [&](){
		inserted = db.LoadServerCertificate(handle,
						    *cert, *key, wrap_key.first,
						    wrap_key.second);
	});

	printf("%s: %s\n", inserted ? "insert" : "update", common_name.c_str());
	db.NotifyModified();
}

static void
ReloadCertificate(const CertDatabaseConfig &db_config, const char *handle)
{
	const ScopeSslGlobalInit ssl_init;

	CertDatabase db(db_config);

	auto cert_key = db.GetServerCertificateKeyByHandle(handle);
	if (!cert_key.second)
		throw "Certificate not found";

	WrapKeyHelper wrap_key_helper;
	const auto wrap_key = wrap_key_helper.SetEncryptKey(db_config);

	db.LoadServerCertificate(handle,
				 *cert_key.first, *cert_key.second,
				 wrap_key.first, wrap_key.second);
}

static void
DeleteCertificate(const CertDatabaseConfig &db_config, const char *handle)
{
	CertDatabase db(db_config);

	const auto result = db.DeleteServerCertificateByHandle(handle);
	if (result.GetAffectedRows() == 0)
		throw "Certificate not found";

	db.NotifyModified();
}

static void
GetCertificate(const CertDatabaseConfig &db_config, const char *handle)
{
	const ScopeSslGlobalInit ssl_init;
	CertDatabase db(db_config);
	auto cert = db.GetServerCertificateByHandle(handle);
	if (!cert)
		throw "Certificate not found";

	X509_print_fp(stdout, cert.get());
	PEM_write_X509(stdout, cert.get());
}

/**
 * Load the private key for the given host name from the database.
 *
 * Returns the key or nullptr if no such certificate/key pair was
 * found.  Throws an exception on error.
 */
static UniqueEVP_PKEY
FindKeyByName(CertDatabase &db, const char *common_name)
{
	return db.GetServerCertificateKey(common_name).second;
}

static void
FindPrintCertificates(CertDatabase &db, const char *name)
{
	for (const auto &row : db.FindServerCertificatesByName(name))
		printf("%s\t%s\t%s\t%s\n",
		       row.GetValue(0), row.GetValue(1),
		       row.GetValue(2), row.GetValue(3));
}

static void
FindCertificate(const CertDatabaseConfig &db_config, const char *host, bool headers)
{
	if (headers)
		printf("id\thandle\tissuer\tnot_after\n");

	const ScopeSslGlobalInit ssl_init;
	CertDatabase db(db_config);

	FindPrintCertificates(db, host);

	const auto wildcard = MakeCommonNameWildcard(host);
	if (!wildcard.empty())
		FindPrintCertificates(db, wildcard.c_str());
}

static void
DumpKey(const CertDatabaseConfig &db_config, const char *host)
{
	const ScopeSslGlobalInit ssl_init;
	CertDatabase db(db_config);

	auto key = FindKeyByName(db, host);
	if (!key)
		throw "Key not found";

	if (PEM_write_PrivateKey(stdout, key.get(), nullptr, nullptr, 0,
				 nullptr, nullptr) <= 0)
		throw SslError("Failed to dump key");
}

gcc_noreturn
static void
Monitor(const CertDatabaseConfig &db_config)
{
	CertDatabase db(db_config);
	db.ListenModified();

	std::string last_modified = db.GetLastModified();
	if (last_modified.empty()) {
		last_modified = db.GetCurrentTimestamp();
		if (last_modified.empty())
			throw "CURRENT_TIMESTAMP failed";
	}

	const FileDescriptor fd(db.GetSocket());

	while (true) {
		if (fd.WaitReadable(-1) < 0)
			throw "poll() failed";

		db.ConsumeInput();
		while (db.GetNextNotify()) {}

		std::string new_last_modified = db.GetLastModified();
		if (new_last_modified.empty())
			throw "No MAX(modified) found";

		for (auto &row : db.GetModifiedServerCertificatesMeta(last_modified.c_str()))
			printf("%s %s %s\n",
			       row.GetValue(1),
			       *row.GetValue(0) == 't' ? "deleted" : "modified",
			       row.GetValue(2));

		last_modified = std::move(new_last_modified);
	}
}

static void
Tail(const CertDatabaseConfig &db_config)
{
	CertDatabase db(db_config);

	for (auto &row : db.TailModifiedServerCertificatesMeta())
		printf("%s %s %s\n",
		       row.GetValue(1),
		       *row.GetValue(0) == 't' ? "deleted" : "modified",
		       row.GetValue(2));
}

static void
CopyCommonName(X509_REQ &req, X509 &src)
{
	X509_NAME *src_subject = X509_get_subject_name(&src);
	if (src_subject == nullptr)
		return;

	int i = X509_NAME_get_index_by_NID(src_subject, NID_commonName, -1);
	if (i < 0)
		return;

	auto *common_name = X509_NAME_get_entry(src_subject, i);
	auto *dest_subject = X509_REQ_get_subject_name(&req);
	X509_NAME_add_entry(dest_subject, common_name, -1, 0);
}

/**
 * Add a subject_alt_name extension for each host name in the list.
 */
template<typename L>
static void
AddDnsAltNames(X509_REQ &req, const L &hosts)
{
	OpenSSL::UniqueGeneralNames ns;
	for (const auto &host : hosts)
		ns.push_back(OpenSSL::ToDnsName(host.c_str()));

	AddAltNames(req, ns);
}

/**
 * Copy the subject_alt_name extension from the source certificate to
 * the request.
 */
static void
CopyDnsAltNames(X509_REQ &req, X509 &src)
{
	int i = X509_get_ext_by_NID(&src, NID_subject_alt_name, -1);
	if (i < 0)
		/* no subject_alt_name found, no-op */
		return;

	auto ext = X509_get_ext(&src, i);
	if (ext == nullptr)
		return;

	OpenSSL::UniqueGeneralNames gn(reinterpret_cast<GENERAL_NAMES *>(X509V3_EXT_d2i(ext)));
	if (!gn)
		return;

	AddAltNames(req, gn);
}

static UniqueX509_REQ
MakeCertRequest(EVP_PKEY &key, const std::set<std::string> &alt_hosts)
{
	UniqueX509_REQ req(X509_REQ_new());
	if (req == nullptr)
		throw "X509_REQ_new() failed";

	if (!alt_hosts.empty())
		AddDnsAltNames(*req, alt_hosts);

	X509_REQ_set_pubkey(req.get(), &key);

	if (!X509_REQ_sign(req.get(), &key, EVP_sha1()))
		throw SslError("X509_REQ_sign() failed");

	return req;
}

static UniqueX509_REQ
MakeCertRequest(EVP_PKEY &key, X509 &src)
{
	UniqueX509_REQ req(X509_REQ_new());
	if (req == nullptr)
		throw "X509_REQ_new() failed";

	CopyCommonName(*req, src);
	CopyDnsAltNames(*req, src);

	X509_REQ_set_pubkey(req.get(), &key);

	if (!X509_REQ_sign(req.get(), &key, EVP_sha1()))
		throw SslError("X509_REQ_sign() failed");

	return req;
}

using Dns01ChallengeRecordPtr = std::shared_ptr<Dns01ChallengeRecord>;
using Dns01ChallengeRecordMap = std::map<std::string, Dns01ChallengeRecordPtr>;

struct PendingAuthorization {
	std::string url;

	std::variant<Http01ChallengeFile,
		     Dns01ChallengeRecordPtr> challenge;

	struct Http01 {};

	PendingAuthorization(Http01,
			     const std::string &_url,
			     const std::string &directory,
			     const AcmeChallenge &_challenge,
			     EVP_PKEY &account_key)
		:url(_url),
		 challenge(std::in_place_type_t<Http01ChallengeFile>{},
			   directory, _challenge, account_key) {}
	struct Dns01 {};

	PendingAuthorization(Dns01,
			     const std::string &_url,
			     const Dns01ChallengeRecordPtr &ptr) noexcept
		:url(_url), challenge(ptr)
	{
	}
};

static const AcmeChallenge *
SelectChallenge(const AcmeConfig &config,
		EVP_PKEY &account_key,
		const std::string &authz_url,
		const AcmeAuthorization &authz_response,
		Dns01ChallengeRecordMap &dns_map,
		std::forward_list<PendingAuthorization> &pending_authz)
{
	if (!config.challenge_directory.empty()) {
		const auto *challenge =
			authz_response.FindChallengeByType("http-01");
		if (challenge != nullptr) {
			pending_authz.emplace_front(PendingAuthorization::Http01{},
						    authz_url,
						    config.challenge_directory,
						    *challenge, account_key);
			return challenge;
		}
	}

	if (!config.dns_txt_program.empty()) {
		const auto *challenge =
			authz_response.FindChallengeByType("dns-01");
		if (challenge != nullptr) {
			auto e = dns_map.emplace(authz_response.identifier,
						 std::make_shared<Dns01ChallengeRecord>(config,
											authz_response.identifier));
			e.first->second->AddChallenge(*challenge, account_key);
			pending_authz.emplace_front(PendingAuthorization::Dns01{},
						    authz_url, e.first->second);
			return challenge;
		}
	}

	return nullptr;
}

static bool
ValidateIdentifier(const AcmeAuthorization &authz,
		   const std::set<std::string> &identifiers) noexcept
{
	return identifiers.find(authz.identifier) != identifiers.end() ||
		/* if a wildcard certificate is requested, the ACME
		   server strips the "*." from the specified
		   identifier; this search re-adds it for the
		   lookup */
		identifiers.find("*." + authz.identifier) != identifiers.end();
}

static auto
CollectPendingAuthorizations(const AcmeConfig &config,
			     EVP_PKEY &account_key,
			     AcmeClient &client,
			     StepProgress &progress,
			     const std::set<std::string> &identifiers,
			     const std::forward_list<std::string> &authorizations)
{
	std::forward_list<PendingAuthorization> pending_authz;

	/* this map is used to construct exactly one
	   Dns01ChallengeRecord instance for each domain, to be shared
	   by multiple authorizations for the same domain, with
	   different values for each authorization; this creates
	   multiple TXT records (and removes the when finished) */
	Dns01ChallengeRecordMap dns_map;

	for (const auto &i : authorizations) {
		auto ar = client.Authorize(account_key, i.c_str());
		if (!ValidateIdentifier(ar, identifiers))
			throw FormatRuntimeError("Invalid identifier received: '%s'",
						 ar.identifier.c_str());

		const auto *challenge = SelectChallenge(config, account_key,
							i, ar, dns_map,
							pending_authz);
		if (challenge == nullptr)
			throw std::runtime_error("No compatible challenge found");

		progress();

		auto challenge2 = client.UpdateChallenge(account_key, *challenge);
		challenge2.Check();
		progress();
	}

	/* now actually set the TXT records we collected previously;
	   after that, the map will be removed, but the
	   std::shared_ptr references will live on in the
	   PendingAuthorization instances, and ~Dns01ChallengeRecord()
	   will be called when the last PendingAuthorization for that
	   domain has finished */
	for (auto &i : dns_map)
		i.second->Commit();

	return pending_authz;
}

static void
AcmeAuthorize(const AcmeConfig &config,
	      EVP_PKEY &account_key,
	      AcmeClient &client,
	      StepProgress &progress,
	      const std::set<std::string> &identifiers,
	      const std::forward_list<std::string> &authorizations)
{
	auto pending_authz = CollectPendingAuthorizations(config, account_key,
							  client, progress,
							  identifiers,
							  authorizations);
	progress();

	while (!pending_authz.empty()) {
		std::this_thread::sleep_for(std::chrono::milliseconds(250));

		for (auto prev = pending_authz.before_begin(), i = std::next(prev);
		     i != pending_authz.end(); i = std::next(prev)) {
			auto authorization = client.PollAuthorization(account_key,
								      i->url.c_str());
			for (const auto &challenge : authorization.challenges)
				challenge.Check();

			switch (authorization.status) {
			case AcmeAuthorization::Status::PENDING:
				break;

			case AcmeAuthorization::Status::VALID:
				pending_authz.erase_after(prev);
				progress();
				continue;

			case AcmeAuthorization::Status::INVALID:
			case AcmeAuthorization::Status::DEACTIVATED:
			case AcmeAuthorization::Status::EXPIRED:
			case AcmeAuthorization::Status::REVOKED:
				throw FormatRuntimeError("Authorization has turned '%s'",
							 AcmeAuthorization::FormatStatus(authorization.status));
			}

			++prev;
		}
	}
}

static void
AcmeNewOrder(const CertDatabaseConfig &db_config, const AcmeConfig &config,
	     EVP_PKEY &account_key,
	     CertDatabase &db,
	     AcmeClient &client,
	     WorkshopProgress _progress,
	     const char *handle,
	     const std::set<std::string> &identifiers)
{
	if (config.challenge_directory.empty() &&
	    config.dns_txt_program.empty())
		throw "Neither --challenge-directory nor --dns-txt-program specified";

	AcmeClient::OrderRequest order_request;
	size_t n_identifiers = 0;
	for (const auto &i : identifiers) {
		order_request.identifiers.emplace_front(i);
		++n_identifiers;
	}

	StepProgress progress(_progress,
			      n_identifiers * 3 + 5);

	const auto order = client.NewOrder(account_key,
					   std::move(order_request));
	progress();

	AcmeAuthorize(config, account_key, client, progress,
		      identifiers, order.authorizations);

	const auto cert_key = GenerateRsaKey();
	const auto req = MakeCertRequest(*cert_key, identifiers);

	const auto order2 = client.FinalizeOrder(account_key, order, *req);
	progress();

	const auto cert = client.DownloadCertificate(account_key, order2);
	progress();

	WrapKeyHelper wrap_key_helper;
	const auto wrap_key = wrap_key_helper.SetEncryptKey(db_config);

	db.DoSerializableRepeat(8, [&](){
		db.LoadServerCertificate(handle, *cert, *cert_key,
					 wrap_key.first, wrap_key.second);
	});

	db.NotifyModified();

	progress();
}

static std::set<std::string>
AllNames(X509 &cert)
{
	std::set<std::string> result;

	for (auto &i : GetSubjectAltNames(cert))
		if (!IsAcmeInvalid(i))
			/* ignore "*.acme.invalid" */
			result.emplace(std::move(i));

	const auto cn = GetCommonName(cert);
	if (!cn.IsNull())
		result.emplace(cn.c_str());

	return result;
}

static void
AcmeRenewCert(const CertDatabaseConfig &db_config, const AcmeConfig &config,
	      EVP_PKEY &account_key,
	      CertDatabase &db, AcmeClient &client,
	      WorkshopProgress _progress,
	      const char *handle)
{
	if (config.challenge_directory.empty() &&
	    config.dns_txt_program.empty())
		throw "Neither --challenge-directory nor --dns-txt-program specified";

	const auto old_cert_key = db.GetServerCertificateKeyByHandle(handle);
	if (!old_cert_key.second)
		throw "Old certificate not found in database";

	auto &old_cert = *old_cert_key.first;
	auto &old_key = *old_cert_key.second;

	const auto names = AllNames(old_cert);
	StepProgress progress(_progress,
			      names.size() * 3 + 5);

	AcmeClient::OrderRequest order_request;
	for (const auto &host : names)
		order_request.identifiers.emplace_front(host);

	const auto order = client.NewOrder(account_key,
					   std::move(order_request));
	progress();

	AcmeAuthorize(config, account_key, client, progress,
		      names, order.authorizations);

	const auto req = MakeCertRequest(old_key, old_cert);

	const auto order2 = client.FinalizeOrder(account_key, order, *req);
	progress();

	const auto cert = client.DownloadCertificate(account_key, order2);
	progress();

	WrapKeyHelper wrap_key_helper;
	const auto wrap_key = wrap_key_helper.SetEncryptKey(db_config);

	db.DoSerializableRepeat(8, [&](){
		db.LoadServerCertificate(handle, *cert, old_key,
					 wrap_key.first, wrap_key.second);
	});

	db.NotifyModified();

	progress();
}

static void
Acme(ConstBuffer<const char *> args)
{
	AcmeConfig config;

	while (!args.empty() && args.front()[0] == '-') {
		const char *arg = args.front();

		if (strcmp(arg, "--staging") == 0) {
			args.shift();
			config.staging = true;
		} else if (strcmp(arg, "--debug") == 0) {
			args.shift();
			config.debug = true;
		} else if (strcmp(arg, "--fake") == 0) {
			/* undocumented debugging option: no HTTP requests, fake
			   ACME responses */
			args.shift();
			config.fake = true;
		} else if (strcmp(arg, "--account-key") == 0) {
			args.shift();

			if (args.empty())
				throw std::runtime_error("File missing");

			config.account_key_path = args.front();
			args.shift();
		} else if (strcmp(arg, "--account-key-id") == 0) {
			args.shift();

			if (args.empty())
				throw std::runtime_error("Key id missing");

			config.account_key_id = args.front();
			args.shift();
		} else if (strcmp(arg, "--challenge-directory") == 0) {
			args.shift();

			if (args.empty())
				throw std::runtime_error("Directory missing");

			config.challenge_directory = args.front();
			args.shift();
		} else if (StringIsEqual(arg, "--dns-txt-program")) {
			args.shift();

			if (args.empty())
				throw std::runtime_error("Program missing");

			config.dns_txt_program = args.front();
			args.shift();
		} else
			break;
	}

	if (args.empty())
		throw "acme commands:\n"
			"  new-reg EMAIL\n"
			"  new-order HANDLE HOST...\n"
			"  renew-cert HANDLE\n"
			"\n"
			"options:\n"
			"  --staging     use the Let's Encrypt staging server\n"
			"  --debug       enable debug mode\n"
			"  --account-key FILE\n"
			"                load the ACME account key from this file\n"
			"  --dns-txt-program PATH\n"
			"                use this program to set TXT records for dns-01\n"
			"  --challenge-directory PATH\n"
			"                use http-01 with this challenge directory\n";

	const char *key_path = config.account_key_path.c_str();

	const auto cmd = args.shift();

	if (strcmp(cmd, "new-reg") == 0) {
		if (args.size != 1)
			throw Usage("acme new-reg EMAIL");

		const char *email = args[0];

		const ScopeSslGlobalInit ssl_init;
		const AcmeKey key(key_path);

		const auto account = AcmeClient(config).NewAccount(*key, email);
		printf("location: %s\n", account.location.c_str());
	} else if (strcmp(cmd, "new-order") == 0) {
		if (args.size < 2)
			throw Usage("acme new-order HANDLE HOST ...");

		const char *handle = args.shift();

		std::set<std::string> identifiers;
		for (const char *i : args)
			identifiers.emplace(i);

		const ScopeSslGlobalInit ssl_init;
		const AcmeKey key(key_path);

		const auto db_config = LoadPatchCertDatabaseConfig();
		CertDatabase db(db_config);
		AcmeClient client(config);

		AcmeNewOrder(db_config, config, *key, db, client, root_progress,
			     handle, identifiers);
		printf("OK\n");
	} else if (strcmp(cmd, "renew-cert") == 0) {
		if (args.size != 1)
			throw Usage("acme renew-cert HANDLE");

		const char *handle = args.front();

		const ScopeSslGlobalInit ssl_init;
		const AcmeKey key(key_path);

		const auto db_config = LoadPatchCertDatabaseConfig();
		CertDatabase db(db_config);
		AcmeClient client(config);

		AcmeRenewCert(db_config, config, *key,
			      db, client, root_progress, handle);

		printf("OK\n");
	} else
		throw "Unknown acme command";
}

static void
Populate(CertDatabase &db, EVP_PKEY *key, ConstBuffer<void> key_der,
	 const char *common_name)
{
	(void)key;

	// TODO: fake time stamps
	const char *not_before = "1971-01-01";
	const char *not_after = "1971-01-01";

	auto cert = MakeSelfSignedDummyCert(*key, common_name);
	db.InsertServerCertificate(nullptr, common_name, common_name,
				   not_before, not_after,
				   *cert, key_der,
				   nullptr);
}

static void
Populate(const CertDatabaseConfig &db_config,
	 const char *key_path, const char *suffix, unsigned n)
{
	const ScopeSslGlobalInit ssl_init;

	const auto key = LoadKeyFile(key_path);

	const SslBuffer key_buffer(*key);

	CertDatabase db(db_config);

	if (n == 0) {
		Populate(db, key.get(), key_buffer.get(), suffix);
	} else {
		db.DoSerializableRepeat(2, [&](){
			for (unsigned i = 1; i <= n; ++i) {
				char buffer[256];
				snprintf(buffer, sizeof(buffer), "%u%s", i, suffix);
				Populate(db, key.get(), key_buffer.get(), buffer);
			}
		});
	}

	db.NotifyModified();
}

static void
HandleLoad(ConstBuffer<const char *> args)
{
	if (args.size != 3)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	LoadCertificate(db_config, args[0], args[1], args[2]);
}

static void
HandleReload(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	ReloadCertificate(db_config, args[0]);
}

static void
HandleDelete(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	DeleteCertificate(db_config, args[0]);
}

static void
PrintNames(const CertDatabaseConfig &db_config, const char *handle)
{
	CertDatabase db(db_config);
	for (const auto &name : db.GetNamesByHandle(handle))
		printf("%s\n", name.c_str());
}

static void
HandleNames(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	PrintNames(db_config, args[0]);
}

static void
HandleGet(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	GetCertificate(db_config, args[0]);
}

static void
HandleFind(ConstBuffer<const char *> args)
{
	bool headers = false;

	while (!args.empty() && args.front()[0] == '-') {
		const char *arg = args.front();

		if (strcmp(arg, "--headers") == 0) {
			args.shift();
			headers = true;
		} else
			break;
	}

	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	FindCertificate(db_config, args[0], headers);
}

static void
SetHandle(const CertDatabaseConfig &db_config,
	  Pg::Serial id, const char *handle)
{
	CertDatabase db(db_config);
	db.SetHandle(id, handle);
}

static void
HandleSetHandle(ConstBuffer<const char *> args)
{
	if (args.size != 2)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	SetHandle(db_config, Pg::Serial::Parse(args[0]), args[1]);
}

static void
HandleDumpKey(ConstBuffer<const char *> args)
{
	if (args.size != 1)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	DumpKey(db_config, args[0]);
}

gcc_noreturn
static void
HandleMonitor(ConstBuffer<const char *> args)
{
	if (args.size != 0)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	Monitor(db_config);
}

static void
HandleTail(ConstBuffer<const char *> args)
{
	if (args.size != 0)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	Tail(db_config);
}

static void
HandleAcme(ConstBuffer<const char *> args)
{
	Acme(args);
}

static void
HandleGenwrap(ConstBuffer<const char *> args)
{
	if (args.size != 0)
		throw AutoUsage();

	CertDatabaseConfig::AES256 key;
	UrandomFill(&key, sizeof(key));

	for (auto b : key)
		printf("%02x", b);
	printf("\n");
}

static void
HandlePopulate(ConstBuffer<const char *> args)
{
	if (args.size < 2 || args.size > 3)
		throw AutoUsage();

	const char *key = args[0];
	const char *suffix = args[1];
	unsigned count = 0;

	if (args.size == 3) {
		count = strtoul(args[2], nullptr, 10);
		if (count == 0)
			throw std::runtime_error("Invalid COUNT parameter");
	}

	const auto db_config = LoadPatchCertDatabaseConfig();
	Populate(db_config, key, suffix, count);
}

static void
HandleMigrate(ConstBuffer<const char *> args)
{
	if (args.size != 0)
		throw AutoUsage();

	const auto db_config = LoadPatchCertDatabaseConfig();
	CertDatabase db(db_config);
	db.Migrate();
}

static constexpr struct Command {
	const char *name, *usage;
	void (*function)(ConstBuffer<const char *> args);
	bool undocumented = false;

	constexpr Command(const char *_name, const char *_usage,
			  void (*_function)(ConstBuffer<const char *> args),
			  bool _undocumented = false)
		:name(_name), usage(_usage),
		 function(_function), undocumented(_undocumented) {}

} commands[] = {
	{ "load", "HANDLE CERT KEY", HandleLoad },
	{ "reload", "HANDLE", HandleReload, true },
	{ "delete", "HANDLE", HandleDelete },
	{ "names", "HANDLE", HandleNames },
	{ "get", "HANDLE", HandleGet },
	{ "find", "[--headers] HOST", HandleFind },
	{ "set-handle", "ID HANDLE", HandleSetHandle },
	{ "dumpkey", "HOST", HandleDumpKey, true },
	{ "monitor", nullptr, HandleMonitor },
	{ "tail", nullptr, HandleTail },
	{ "acme", "[OPTIONS] COMMAND ...", HandleAcme },
	{ "genwrap", "", HandleGenwrap },
	{ "populate", "KEY SUFFIX COUNT", HandlePopulate, true },
	{ "migrate", nullptr, HandleMigrate },
};

static const Command *
FindCommand(const char *name)
{
	for (const auto &i : commands)
		if (strcmp(i.name, name) == 0)
			return &i;

	return nullptr;
}

int
main(int argc, char **argv)
try {
	ConstBuffer<const char *> args(argv + 1, argc - 1);

	while (!args.empty() && *args.front() == '-') {
		if (strcmp(args.front(), "--progress") == 0) {
			args.shift();
			root_progress = WorkshopProgress(0, 100);
		} else if (strncmp(args.front(), "--progress=", 11) == 0) {
			const char *range = args.front() + 11;
			args.shift();

			char *endptr;
			unsigned min = strtoul(range, &endptr, 10);
			if (endptr == range || *endptr != '-' || min > 100)
				throw "Failed to parse progress range";

			range = endptr + 1;
			unsigned max = strtoul(range, &endptr, 10);
			if (endptr == range || *endptr != 0 || max < min || max > 100)
				throw "Failed to parse progress range";

			root_progress = WorkshopProgress(min, max);
		} else if (strcmp(args.front(), "--workshop-control") == 0) {
			args.shift();
			root_progress.UseControlChannel();
		} else {
			fprintf(stderr, "Unknown option: %s\n\n", args.front());
			/* clear the list to trigger printing the usage */
			args.size = 0;
		}
	}

	if (args.empty()) {
		fprintf(stderr, "Usage: %s [OPTIONS] COMMAND ...\n"
			"\n"
			"Commands:\n", argv[0]);

		for (const auto &i : commands) {
			if (i.undocumented)
				continue;

			if (i.usage != nullptr)
				fprintf(stderr, "  %s %s\n", i.name, i.usage);
			else
				fprintf(stderr, "  %s\n", i.name);
		}

		fprintf(stderr, "\n"
			"Global options:\n"
			"  --progress[=MIN,MAX]  print Workshop job progress\n"
			"  --workshop-control    use the Workshop contrl channel for progress\n");

		return EXIT_FAILURE;
	}

	/* force line buffering, because this program may be used
	   non-interactively, and mixing stdout/stderr is confusing in
	   block-buffered mode */
	setvbuf(stdout, nullptr, _IOLBF, 0);
	setvbuf(stderr, nullptr, _IOLBF, 0);

	const auto cmd = args.shift();

	const auto *cmd2 = FindCommand(cmd);
	if (cmd2 == nullptr) {
		fprintf(stderr, "Unknown command: %s\n", cmd);
		return EXIT_FAILURE;
	}

	try {
		cmd2->function(args);
	} catch (AutoUsage) {
		if (cmd2->usage != nullptr)
			fprintf(stderr, "Usage: %s %s %s\n", argv[0],
				cmd2->name, cmd2->usage);
		else
			fprintf(stderr, "Usage: %s %s\n", argv[0],
				cmd2->name);
		return EXIT_FAILURE;
	}

	return EXIT_SUCCESS;
} catch (const std::exception &e) {
	PrintException(e);
	return EXIT_FAILURE;
} catch (Usage u) {
	fprintf(stderr, "Usage: %s %s\n", argv[0], u.text);
	return EXIT_FAILURE;
} catch (const char *msg) {
	fprintf(stderr, "%s\n", msg);
	return EXIT_FAILURE;
}
