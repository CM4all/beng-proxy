// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "AcmeAlpn.hxx"
#include "AcmeHttp.hxx"
#include "CertDatabase.hxx"
#include "Config.hxx"
#include "lib/openssl/Dummy.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/Edit.hxx"
#include "lib/openssl/Key.hxx"
#include "lib/sodium/SHA256.hxx"
#include "util/HexFormat.hxx"
#include "util/PrintException.hxx"
#include "util/ScopeExit.hxx"
#include "util/SpanCast.hxx"

#include <fmt/core.h>

[[gnu::const]]
static int
GetAcmeIdentifierObjectId() noexcept
{
	const char *const txt = "1.3.6.1.5.5.7.1.31";

	if (int id = OBJ_txt2nid(txt); id != NID_undef)
		return id;

	return OBJ_create(txt, "pe-acmeIdentifier", "ACME Identifier");
}

static std::string
MakeCommonName(std::string_view host) noexcept
{
	if (host.size() <= 64)
		return std::string{host};

	/* if the host name is too long for the common_name, use the
	   (abbreviated) SHA256 digest instead; the real host name is
	   in subjectAltName, and the common_name is meaningless for
	   acme-alpn */

	const auto sha256 = SHA256(AsBytes(host));
	const auto hex = HexFormat(std::span{sha256}.first<20>());

	std::string result{"acme-tls-alpn-01:"};
	result.append(std::string_view{hex.data(), hex.size()});
	return result;
}

Alpn01ChallengeRecord::Alpn01ChallengeRecord(CertDatabase &_db,
					     std::string_view _host)
	:db(_db), host(_host),
	 handle(std::string{"acme-tls-alpn-01:"} + host),
	 cert(MakeSelfIssuedDummyCert(MakeCommonName(host)))
{
	std::string alt_name = std::string{"DNS:"} + host;

	AddExt(*cert, NID_subject_alt_name, alt_name.c_str());
}

Alpn01ChallengeRecord::~Alpn01ChallengeRecord() noexcept
{
	try {
		db.DeleteServerCertificateByHandle(handle.c_str());
	} catch (...) {
		fmt::print("Failed to remove certdb record of '{}': ",
			   host);
		PrintException(std::current_exception());
	}
}

void
Alpn01ChallengeRecord::AddChallenge(const AcmeChallenge &challenge,
				    const EVP_PKEY &account_key)
{
	struct {
		uint8_t type = 0x04, size;
		SHA256DigestBuffer payload;
	} value;

	value.size = sizeof(value.payload);
	value.payload = SHA256(AsBytes(MakeHttp01(challenge, account_key)));

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

	const auto [wrap_key_name, wrap_key] = db_config.GetDefaultWrapKey();

	db.LoadServerCertificate(handle.c_str(), "acme-alpn-tls-01",
				 *cert, *cert_key,
				 wrap_key_name, wrap_key);
	db.NotifyModified();
}
