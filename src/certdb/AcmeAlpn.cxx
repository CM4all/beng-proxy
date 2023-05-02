// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "AcmeAlpn.hxx"
#include "AcmeHttp.hxx"
#include "AcmeChallenge.hxx"
#include "CertDatabase.hxx"
#include "WrapKey.hxx"
#include "lib/openssl/Dummy.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/Edit.hxx"
#include "lib/openssl/Key.hxx"
#include "lib/sodium/UrlSafeBase64SHA256.hxx"
#include "util/PrintException.hxx"
#include "util/ScopeExit.hxx"

#include <boost/json.hpp>

[[gnu::const]]
static int
GetAcmeIdentifierObjectId() noexcept
{
	const char *const txt = "1.3.6.1.5.5.7.1.31";

	if (int id = OBJ_txt2nid(txt); id != NID_undef)
		return id;

	return OBJ_create(txt, "pe-acmeIdentifier", "ACME Identifier");
}

Alpn01ChallengeRecord::Alpn01ChallengeRecord(CertDatabase &_db,
					     const std::string &_host)
	:db(_db), host(_host),
	 handle(std::string{"acme-tls-alpn-01:"} + host),
	 cert(MakeSelfIssuedDummyCert(host))
{
	std::string alt_name = std::string{"DNS:"} + host;

	AddExt(*cert, NID_subject_alt_name, alt_name.c_str());
}

Alpn01ChallengeRecord::~Alpn01ChallengeRecord() noexcept
{
	try {
		db.DeleteServerCertificateByHandle(handle.c_str());
	} catch (...) {
		fprintf(stderr, "Failed to remove certdb record of '%s': ",
			host.c_str());
		PrintException(std::current_exception());
	}
}

void
Alpn01ChallengeRecord::AddChallenge(const AcmeChallenge &challenge,
				    EVP_PKEY &account_key)
{
	struct {
		uint8_t type = 0x04, size;
		SHA256Digest payload;
	} value;

	value.size = sizeof(value.payload);
	value.payload = SHA256(MakeHttp01(challenge, account_key));

	const int nid = GetAcmeIdentifierObjectId();

	auto *s = ASN1_OCTET_STRING_new();
	ASN1_OCTET_STRING_set(s, (const unsigned char *)&value,
			      sizeof(value));
	AtScopeExit(s) { ASN1_OCTET_STRING_free(s); };

	auto *ext = X509_EXTENSION_create_by_NID(nullptr, nid, 1, s);
	AtScopeExit(ext) { X509_EXTENSION_free(ext); };
	X509_add_ext(cert.get(), ext, -1);
}

void
Alpn01ChallengeRecord::Commit(const CertDatabaseConfig &db_config)
{
	const auto cert_key = GenerateEcKey();

	X509_set_pubkey(cert.get(), cert_key.get());
	if (!X509_sign(cert.get(), cert_key.get(), EVP_sha256()))
		throw SslError("X509_sign() failed");

	WrapKeyHelper wrap_key_helper;
	const auto wrap_key = wrap_key_helper.SetEncryptKey(db_config);

	db.LoadServerCertificate(handle.c_str(), "acme-alpn-tls-01",
				 *cert, *cert_key,
				 wrap_key.first, wrap_key.second);
	db.NotifyModified();
}
