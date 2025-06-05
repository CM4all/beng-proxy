// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Puki.hxx"
#include "Main.hxx"
#include "CertDatabase.hxx"
#include "Config.hxx"
#include "CRequest.hxx"
#include "lib/curl/Easy.hxx"
#include "lib/curl/Slist.hxx"
#include "lib/curl/StringGlue.hxx"
#include "lib/curl/StringResponse.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/Buffer.hxx"
#include "lib/openssl/Key.hxx" // for GenerateEcKey()
#include "lib/openssl/MemBio.hxx" // for BioWriterToString()
#include "lib/openssl/UniqueBIO.hxx"
#include "lib/openssl/UniqueCertKey.hxx"
#include "http/Status.hxx"
#include "util/ConstBuffer.hxx"
#include "util/MimeType.hxx"
#include "util/StringAPI.hxx"

#include <set>
#include <string>

using std::string_view_literals::operator""sv;

struct PukiConfig {
	const char *puki_url = nullptr;

	const char *tls_ca = nullptr;

	bool verbose = false;
};

static UniqueX509
ObtainPukiCertificate(const PukiConfig &config, X509_REQ &req)
{
	const auto req_pem = BioWriterToString([&req](BIO &bio){
		PEM_write_bio_X509_REQ(&bio, &req);
	});

	CurlSlist request_headers;

	CurlEasy easy{config.puki_url};
	easy.SetOption(CURLOPT_VERBOSE, long(config.verbose));

	if (config.tls_ca != nullptr)
		easy.SetOption(CURLOPT_CAINFO, config.tls_ca);

	easy.SetRequestBody(req_pem.c_str());

	request_headers.Append("Content-Type: text/plain");
	easy.SetRequestHeaders(request_headers.Get());

	const auto response = StringCurlRequest(std::move(easy));

	if (!http_status_is_success(response.status))
		throw FmtRuntimeError("Status {} from PUKI: {}",
				      static_cast<unsigned>(response.status),
				      response.body);

	if (auto ct = response.headers.find("content-type");
	    ct == response.headers.end() ||
	    GetMimeTypeBase(ct->second) != "application/x-pem-file"sv)
		throw std::runtime_error("Wrong Content-Type in certificate download");

	UniqueBIO in{BIO_new_mem_buf(response.body.data(), response.body.length())};
	return UniqueX509{
		PEM_read_bio_X509(in.get(), nullptr, nullptr, nullptr)
	};
}

static UniqueX509
ObtainPukiCertificate(const PukiConfig &config, EVP_PKEY &key,
		      const std::set<std::string> &hosts)
{
	assert(!hosts.empty());

	const auto req = MakeCertRequest(key, hosts.begin()->c_str(), hosts);
	return ObtainPukiCertificate(config, *req);
}

static UniqueX509
ObtainNewPukiCertificate(const PukiConfig &config, EVP_PKEY &key, X509 &old_cert)
{
	const auto req = MakeCertRequest(key, old_cert);
	return ObtainPukiCertificate(config, *req);
}

static void
NewCert(const CertDatabaseConfig &db_config, const PukiConfig &config,
	CertDatabase &db,
	const char *handle,
	const std::set<std::string> &hosts)
{
	const auto key = GenerateEcKey();
	const auto cert = ObtainPukiCertificate(config, *key, hosts);

	const auto [wrap_key_name, wrap_key] = db_config.GetDefaultWrapKey();

	db.DoSerializableRepeat(8, [&](){
		db.LoadServerCertificate(handle, nullptr, *cert, *key,
					 wrap_key_name, wrap_key);
	});

	db.NotifyModified();
}

static void
RenewCert(const CertDatabaseConfig &db_config, const PukiConfig &config,
	  CertDatabase &db,
	  const char *handle)
{
	const auto old_cert_key = db.GetServerCertificateKeyByHandle(handle);
	if (!old_cert_key)
		throw "Old certificate not found in database";

	auto &old_cert = *old_cert_key.cert;
	auto &old_key = *old_cert_key.key;
	auto &new_key = old_key;

	const auto cert = ObtainNewPukiCertificate(config, new_key, old_cert);

	const auto [wrap_key_name, wrap_key] = db_config.GetDefaultWrapKey();

	db.DoSerializableRepeat(8, [&](){
		db.LoadServerCertificate(handle, nullptr, *cert, new_key,
					 wrap_key_name, wrap_key);
	});

	db.NotifyModified();
}

void
HandlePuki(ConstBuffer<const char *> args)
{
	PukiConfig config;

	while (!args.empty() && args.front()[0] == '-') {
		const char *arg = args.front();

		if (StringIsEqual(arg, "--verbose")) {
			args.shift();
			config.verbose = true;
		} else if (StringIsEqual(arg, "--puki-url")) {
			args.shift();

			if (args.empty())
				throw "URL missing";

			config.puki_url = args.front();
			args.shift();
		} else if (StringIsEqual(arg, "--tls-ca")) {
			args.shift();

			if (args.empty())
				throw "TLS CA filename missing";

			config.tls_ca = args.front();
			args.shift();
		} else
			break;
	}

	if (args.empty())
		throw "puki commands:\n"
			"  new-cert HANDLE HOST...\n"
			"  renew-cert HANDLE\n"
			"\n"
			"options:\n"
			"  --puki-url    the PUKI endpoint URL\n"
			"  --verbose     enable verbose mode\n";

	if (config.puki_url == nullptr)
		throw "No --puki-url parameter";

	const char *const cmd = args.shift();

	if (StringIsEqual(cmd, "new-cert")) {
		if (args.size < 2)
			throw Usage{"puki new-cert HANDLE HOST..."};

		const auto db_config = LoadPatchCertDatabaseConfig();
		CertDatabase db(db_config);

		const char *const handle = args.shift();
		NewCert(db_config, config, db,
			handle, {args.begin(), args.end()});
	} else if (StringIsEqual(cmd, "renew-cert")) {
		if (args.size != 1)
			throw Usage{"puki renew-cert HANDLE"};

		const auto db_config = LoadPatchCertDatabaseConfig();
		CertDatabase db(db_config);

		const char *const handle = args.shift();
		RenewCert(db_config, config, db,
			  handle);
	} else
		throw "Unknown puki command";
}
