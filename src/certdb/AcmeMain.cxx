// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AcmeMain.hxx"
#include "CRequest.hxx"
#include "Main.hxx"
#include "Progress.hxx"
#include "AcmeUtil.hxx"
#include "AcmeClient.hxx"
#include "AcmeAccount.hxx"
#include "AcmeOrder.hxx"
#include "AcmeAuthorization.hxx"
#include "AcmeChallenge.hxx"
#include "AcmeKey.hxx"
#include "AcmeHttp.hxx"
#include "AcmeDns.hxx"
#include "AcmeAlpn.hxx"
#include "AcmeConfig.hxx"
#include "Config.hxx"
#include "CertDatabase.hxx"
#include "WrapKey.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/Key.hxx"
#include "lib/openssl/AltName.hxx"
#include "lib/openssl/Name.hxx"
#include "lib/openssl/UniqueCertKey.hxx"
#include "util/AllocatedString.hxx"
#include <span>
#include "util/StringAPI.hxx"

#include <map>
#include <memory>
#include <thread>
#include <set>
#include <variant>

#include <stdio.h>

static AcmeKey
GetAcmeAccountKey(AcmeConfig &config, CertDatabase &db)
{
	if (config.account_db) {
		auto account = db.GetAcmeAccount(config.staging);
		config.account_key_id = std::move(account.location);
		return AcmeKey(std::move(account.key));
	} else
		return AcmeKey(config.account_key_path.c_str());
}

using Alpn01ChallengeRecordPtr = std::shared_ptr<Alpn01ChallengeRecord>;
using Alpn01ChallengeRecordMap = std::map<std::string, Alpn01ChallengeRecordPtr>;

using Dns01ChallengeRecordPtr = std::shared_ptr<Dns01ChallengeRecord>;
using Dns01ChallengeRecordMap = std::map<std::string, Dns01ChallengeRecordPtr>;

struct PendingAuthorization {
	std::string url;

	std::variant<Http01ChallengeFile,
		     Alpn01ChallengeRecordPtr,
		     Dns01ChallengeRecordPtr> challenge;

	struct Http01 {};

	PendingAuthorization(Http01,
			     const std::string &_url,
			     const std::string &directory,
			     const AcmeChallenge &_challenge,
			     const EVP_PKEY &account_key)
		:url(_url),
		 challenge(std::in_place_type_t<Http01ChallengeFile>{},
			   directory, _challenge, account_key) {}

	struct Alpn01 {};

	PendingAuthorization(Alpn01,
			     const std::string &_url,
			     const Alpn01ChallengeRecordPtr &ptr) noexcept
		:url(_url), challenge(ptr)
	{
	}

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
		CertDatabase &db,
		const std::string &authz_url,
		const AcmeAuthorization &authz_response,
		Alpn01ChallengeRecordMap &alpn_map,
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

	if (config.alpn) {
		const auto *challenge =
			authz_response.FindChallengeByType("tls-alpn-01");
		if (challenge != nullptr) {
			auto e = alpn_map.emplace(authz_response.identifier,
						 std::make_shared<Alpn01ChallengeRecord>(db,
											 authz_response.identifier));
			e.first->second->AddChallenge(*challenge, account_key);
			pending_authz.emplace_front(PendingAuthorization::Alpn01{},
						    authz_url, e.first->second);
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
CollectPendingAuthorizations(const CertDatabaseConfig &db_config,
			     const AcmeConfig &config,
			     EVP_PKEY &account_key,
			     CertDatabase &db,
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
	   multiple TXT records (and removes them when finished) */
	Dns01ChallengeRecordMap dns_map;
	Alpn01ChallengeRecordMap alpn_map;

	std::forward_list<AcmeChallenge> challenges;

	for (const auto &i : authorizations) {
		auto ar = client.Authorize(account_key, i.c_str());
		if (!ValidateIdentifier(ar, identifiers))
			throw FmtRuntimeError("Invalid identifier received: '{}'",
					      ar.identifier);

		if (config.debug) {
			fmt::print(stderr, "ACME authorization: {}\n", ar.identifier);
			for (const auto &c : ar.challenges)
				fmt::print(stderr, "Challenge type={} status={}\n",
					   c.type, c.FormatStatus(c.status));
		}

		const auto *challenge = SelectChallenge(config, account_key, db,
							i, ar,
							alpn_map, dns_map,
							pending_authz);
		if (challenge == nullptr)
			throw std::runtime_error("No compatible challenge found");

		progress();

		/* postpone the challenge update to after the commit */
		challenges.emplace_front(*challenge);
	}

	/* now actually set the TXT records we collected previously;
	   after that, the map will be removed, but the
	   std::shared_ptr references will live on in the
	   PendingAuthorization instances, and ~Dns01ChallengeRecord()
	   will be called when the last PendingAuthorization for that
	   domain has finished */
	for (auto &i : dns_map)
		i.second->Commit();

	for (auto &i : alpn_map)
		i.second->Commit(db_config);

	/* wait for a moment so the changes we just commited can take
	   effect (e.g. beng-lb updates its CertNameCache after
	   receiving a PostgreSQL notification about a new alpn-01
	   certificate) before we ask the ACME server to check them */
	std::this_thread::sleep_for(std::chrono::seconds{1});

	/* update all challenges, which triggers the server-side
	   check */
	while (!challenges.empty()) {
		auto challenge = std::move(challenges.front());
		challenges.pop_front();

		if (challenge.status == AcmeChallenge::Status::PENDING)
			challenge = client.UpdateChallenge(account_key, challenge);

		challenge.Check();

		progress();
	}

	return pending_authz;
}

static void
AcmeAuthorize(const CertDatabaseConfig &db_config, const AcmeConfig &config,
	      EVP_PKEY &account_key,
	      CertDatabase &db,
	      AcmeClient &client,
	      StepProgress &progress,
	      const std::set<std::string> &identifiers,
	      const std::forward_list<std::string> &authorizations)
{
	auto pending_authz = CollectPendingAuthorizations(db_config, config,
							  account_key, db,
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
				throw FmtRuntimeError("Authorization has turned '{}'",
						      AcmeAuthorization::FormatStatus(authorization.status));
			}

			++prev;
		}
	}
}

static AcmeOrder
WaitOrderFinishProcessing(EVP_PKEY &account_key,
			  AcmeClient &client, AcmeOrder &&order)
{
	while (order.status == AcmeOrder::Status::PROCESSING) {
		std::this_thread::sleep_for(std::chrono::seconds{1});
		order = client.PollOrder(account_key, order.location.c_str());
	}

	return std::move(order);
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
	    !config.alpn &&
	    config.dns_txt_program.empty())
		throw "Neither --alpn nor --challenge-directory nor --dns-txt-program specified";

	AcmeOrderRequest order_request;
	size_t n_identifiers = 0;
	for (const auto &i : identifiers) {
		order_request.identifiers.emplace_front(i);
		++n_identifiers;
	}

	StepProgress progress(_progress,
			      n_identifiers * 3 + 6);

	auto order = client.NewOrder(account_key, std::move(order_request));
	progress();

	AcmeAuthorize(db_config, config, account_key, db, client, progress,
		      identifiers, order.authorizations);

	const auto cert_key = GenerateEcKey();
	const auto req = MakeCertRequest(*cert_key, nullptr, identifiers);

	order = client.FinalizeOrder(account_key, order, *req);
	progress();

	order = WaitOrderFinishProcessing(account_key, client, std::move(order));
	if (order.status != AcmeOrder::Status::VALID)
		throw FmtRuntimeError("Bad order status: {}",
				      AcmeOrder::FormatStatus(order.status));

	if (order.certificate.empty())
		throw std::runtime_error{"No certificate URL in valid order"};

	progress();

	const auto cert = client.DownloadCertificate(account_key, order);
	progress();

	const auto [wrap_key_name, wrap_key] = db_config.GetDefaultWrapKey();

	db.DoSerializableRepeat(8, [&](){
		db.LoadServerCertificate(handle, nullptr, *cert, *cert_key,
					 wrap_key_name, wrap_key);
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
	if (cn != nullptr)
		result.emplace(cn.c_str());

	return result;
}

[[gnu::pure]]
static bool
AcceptKey(const EVP_PKEY &key) noexcept
{
	return EVP_PKEY_base_id(&key) == EVP_PKEY_EC;
}

static void
AcmeRenewCert(const CertDatabaseConfig &db_config, const AcmeConfig &config,
	      EVP_PKEY &account_key,
	      CertDatabase &db, AcmeClient &client,
	      WorkshopProgress _progress,
	      const char *handle)
{
	if (config.challenge_directory.empty() &&
	    !config.alpn &&
	    config.dns_txt_program.empty())
		throw "Neither --alpn nor --challenge-directory nor --dns-txt-program specified";

	const auto old_cert_key = db.GetServerCertificateKeyByHandle(handle);
	if (!old_cert_key)
		throw "Old certificate not found in database";

	auto &old_cert = *old_cert_key.cert;
	auto &old_key = *old_cert_key.key;

	UniqueEVP_PKEY generated_key;
	if (!AcceptKey(old_key)) {
		/* migrate old RSA keys to EC */
		generated_key = GenerateEcKey();
		assert(AcceptKey(*generated_key));
	}

	auto &new_key = generated_key ? *generated_key : old_key;

	const auto names = AllNames(old_cert);
	StepProgress progress(_progress,
			      names.size() * 3 + 6);

	AcmeOrderRequest order_request;
	for (const auto &host : names)
		order_request.identifiers.emplace_front(host);

	auto order = client.NewOrder(account_key, std::move(order_request));
	progress();

	AcmeAuthorize(db_config, config, account_key, db, client, progress,
		      names, order.authorizations);

	const auto req = MakeCertRequest(new_key, old_cert);

	order = client.FinalizeOrder(account_key, order, *req);
	progress();

	order = WaitOrderFinishProcessing(account_key, client, std::move(order));
	if (order.status != AcmeOrder::Status::VALID)
		throw FmtRuntimeError("Bad order status: {}",
				      AcmeOrder::FormatStatus(order.status));

	if (order.certificate.empty())
		throw std::runtime_error{"No certificate URL in valid order"};

	progress();

	const auto cert = client.DownloadCertificate(account_key, order);
	progress();

	const auto [wrap_key_name, wrap_key] = db_config.GetDefaultWrapKey();

	db.DoSerializableRepeat(8, [&](){
		db.LoadServerCertificate(handle, nullptr, *cert, new_key,
					 wrap_key_name, wrap_key);
	});

	db.NotifyModified();

	progress();
}

static void
PrintAccount(const AcmeAccount &account) noexcept
{
	fmt::print("status: {}\n", AcmeAccount::FormatStatus(account.status));

	for (const auto &i : account.contact)
		fmt::print("contact: {}\n", i);

	fmt::print("location: {}\n", account.location);
}

void
Acme(std::span<const char *const> args)
{
	AcmeConfig config;

	while (!args.empty() && args.front()[0] == '-') {
		const char *arg = args.front();

		if (StringIsEqual(arg, "--staging")) {
			args = args.subspan(1);
			config.staging = true;
		} else if (StringIsEqual(arg, "--directory-url")) {
			args = args.subspan(1);

			if (args.empty())
				throw std::runtime_error("Directory URL missing");

			config.directory_url = args.front();
			args = args.subspan(1);
		} else if (StringIsEqual(arg, "--tls-ca")) {
			args = args.subspan(1);

			if (args.empty())
				throw std::runtime_error("TLS CA filename missing");

			config.tls_ca = args.front();
			args = args.subspan(1);
		} else if (StringIsEqual(arg, "--debug")) {
			args = args.subspan(1);
			config.debug = true;
		} else if (StringIsEqual(arg, "--account-db")) {
			args = args.subspan(1);
			config.account_db = true;
		} else if (StringIsEqual(arg, "--account-key")) {
			args = args.subspan(1);

			if (args.empty())
				throw std::runtime_error("File missing");

			config.account_key_path = args.front();
			args = args.subspan(1);
		} else if (StringIsEqual(arg, "--account-key-id")) {
			args = args.subspan(1);

			if (args.empty())
				throw std::runtime_error("Key id missing");

			config.account_key_id = args.front();
			args = args.subspan(1);
		} else if (StringIsEqual(arg, "--challenge-directory")) {
			args = args.subspan(1);

			if (args.empty())
				throw std::runtime_error("Directory missing");

			config.challenge_directory = args.front();
			args = args.subspan(1);
		} else if (StringIsEqual(arg, "--dns-txt-program")) {
			args = args.subspan(1);

			if (args.empty())
				throw std::runtime_error("Program missing");

			config.dns_txt_program = args.front();
			args = args.subspan(1);
		} else if (StringIsEqual(arg, "--alpn")) {
			args = args.subspan(1);
			config.alpn = true;
		} else
			break;
	}

	if (args.empty())
		throw "acme commands:\n"
			"  new-account EMAIL\n"
			"  get-account\n"
			"  import-account KEYFILE\n"
			"  new-order HANDLE HOST...\n"
			"  renew-cert HANDLE\n"
			"\n"
			"options:\n"
			"  --staging     use the Let's Encrypt staging server\n"
			"  --directory-url URL\n"
			"                use this ACME server\n"
			"  --tls-ca FILE accept this CA certificate for TLS\n"
			"  --debug       enable debug mode\n"
			"  --account-db  load the ACME account key from the database\n"
			"  --account-key FILE\n"
			"                load the ACME account key from this file\n"
			"  --alpn\n"
			"                enable tls-alpn-01\n"
			"  --dns-txt-program PATH\n"
			"                use this program to set TXT records for dns-01\n"
			"  --challenge-directory PATH\n"
			"                use http-01 with this challenge directory\n";

	const char *key_path = config.account_key_path.c_str();

	const auto cmd = args.front();
	args = args.subspan(1);

	if (StringIsEqual(cmd, "new-account") ||
	    /* deprecated alias: */ StringIsEqual(cmd, "new-reg")) {
		if (args.size() != 1)
			throw Usage("acme new-account EMAIL");

		const char *email = args[0];

		if (config.account_db) {
			/* using the account database: generate a new
			   key, create account and store it in the
			   database */
			const AcmeKey key(GenerateEcKey());
			const auto account = AcmeClient(config).NewAccount(*key, email);

			const auto db_config = LoadPatchCertDatabaseConfig();
			CertDatabase db(db_config);

			const auto [wrap_key_name, wrap_key] = db_config.GetDefaultWrapKey();

			db.InsertAcmeAccount(config.staging, email,
					     account.location.c_str(), *key,
					     wrap_key_name, wrap_key);

			fmt::print("{}\n", account.location);
		} else {
			const AcmeKey key(key_path);
			const auto account = AcmeClient(config).NewAccount(*key, email);
			fmt::print("{}\n", account.location);
		}
	} else if (StringIsEqual(cmd, "get-account")) {
		if (!args.empty())
			throw Usage("acme get-account");

		if (config.account_db) {
			const auto db_config = LoadPatchCertDatabaseConfig();
			CertDatabase db(db_config);
			const auto key = db.GetAcmeAccount(config.staging).key;
			const auto account = AcmeClient(config).NewAccount(*key,
									   nullptr,
									   true);
			PrintAccount(account);
		} else {
			const AcmeKey key(key_path);

			const auto account = AcmeClient(config).NewAccount(*key,
									   nullptr,
									   true);
			PrintAccount(account);
		}
	} else if (StringIsEqual(cmd, "import-account")) {
		if (!config.account_db)
			throw std::runtime_error("import-account requires --account-db");

		if (args.size() != 1)
			throw Usage("acme import-account KEYFILE");

		const char *import_key_path = args.front();
		args = args.subspan(1);

		const auto db_config = LoadPatchCertDatabaseConfig();

		const AcmeKey key(import_key_path);

		const auto account = AcmeClient(config).NewAccount(*key,
								   nullptr,
								   true);

		if (account.status != AcmeAccount::Status::VALID)
			throw Usage("Account is not valid");

		const auto [wrap_key_name, wrap_key] = db_config.GetDefaultWrapKey();

		CertDatabase db(db_config);
		db.InsertAcmeAccount(config.staging,
				     account.GetEmail(),
				     account.location.c_str(), *key,
				     wrap_key_name, wrap_key);

		PrintAccount(account);
	} else if (StringIsEqual(cmd, "new-order")) {
		if (args.size() < 2)
			throw Usage("acme new-order HANDLE HOST ...");

		const char *handle = args.front();
		args = args.subspan(1);

		std::set<std::string> identifiers;
		for (const char *i : args)
			identifiers.emplace(i);

		const auto db_config = LoadPatchCertDatabaseConfig();
		CertDatabase db(db_config);
		const auto key = GetAcmeAccountKey(config, db);
		AcmeClient client(config);

		AcmeNewOrder(db_config, config, *key, db, client, root_progress,
			     handle, identifiers);
		fmt::print("OK\n");
	} else if (StringIsEqual(cmd, "renew-cert")) {
		if (args.size() != 1)
			throw Usage("acme renew-cert HANDLE");

		const char *handle = args.front();

		const auto db_config = LoadPatchCertDatabaseConfig();
		CertDatabase db(db_config);
		const auto key = GetAcmeAccountKey(config, db);
		AcmeClient client(config);

		AcmeRenewCert(db_config, config, *key,
			      db, client, root_progress, handle);

		fmt::print("OK\n");
	} else
		throw "Unknown acme command";
}
