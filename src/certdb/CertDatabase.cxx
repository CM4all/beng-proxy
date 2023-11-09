// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CertDatabase.hxx"
#include "Queries.hxx"
#include "FromResult.hxx"
#include "Config.hxx"
#include "WrapKey.hxx"
#include "lib/openssl/Buffer.hxx"
#include "lib/openssl/Time.hxx"
#include "lib/openssl/Name.hxx"
#include "lib/openssl/AltName.hxx"
#include "lib/openssl/UniqueCertKey.hxx"
#include "io/FileDescriptor.hxx"
#include "util/AllocatedArray.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/aes.h>

/**
 * A callable which invokes Pg::Connection::ExecuteParams().
 */
struct SyncQueryWrapper {
	Pg::Connection &connection;

	template<typename... Params>
	auto operator()(const Params&... params) {
		return connection.ExecuteParams(params...);
	}
};

CertDatabase::CertDatabase(const CertDatabaseConfig &_config)
	:config(_config), conn(config.connect.c_str())
{
	if (!config.schema.empty())
		conn.SetSchema(config.schema.c_str());
}

bool
CertDatabase::CheckConnected()
{
	if (GetStatus() != CONNECTION_OK)
		return false;

	if (FileDescriptor(GetSocket()).WaitReadable(0) == 0)
		return true;

	conn.ConsumeInput();
	if (GetStatus() != CONNECTION_OK)
		return false;

	/* try again, just in case the previous PQconsumeInput() call has
	   read a final message from the socket */
	if (FileDescriptor(GetSocket()).WaitReadable(0) == 0)
		return true;

	conn.ConsumeInput();
	return GetStatus() == CONNECTION_OK;
}

void
CertDatabase::EnsureConnected()
{
	if (CheckConnected())
		return;

	conn.Reconnect();

	if (!config.schema.empty()) {
		try {
			conn.SetSchema(config.schema.c_str());
		} catch (...) {
			conn.Disconnect();
			throw;
		}
	}
}

Pg::Result
CertDatabase::ListenModified()
{
	std::string sql("LISTEN \"");
	if (!config.schema.empty() && config.schema != "public") {
		/* prefix the notify name unless we're in the default
		   schema */
		sql += config.schema;
		sql += ':';
	}

	sql += "modified\"";

	return conn.Execute(sql.c_str());
}

Pg::Result
CertDatabase::NotifyModified()
{
	std::string sql("NOTIFY \"");
	if (!config.schema.empty() && config.schema != "public") {
		/* prefix the notify name unless we're in the default
		   schema */
		sql += config.schema;
		sql += ':';
	}

	sql += "modified\"";

	return conn.Execute(sql.c_str());
}

Pg::Serial
CertDatabase::GetIdByHandle(const char *handle)
{
	auto result = conn.ExecuteParams("SELECT id FROM server_certificate "
					 "WHERE handle=$1 "
					 "LIMIT 1",
					 handle);
	if (result.GetRowCount() == 0)
		return Pg::Serial();

	return Pg::Serial::Parse(result.GetValue(0, 0));
}

void
CertDatabase::InsertServerCertificate(const char *handle,
				      const char *special,
				      const char *common_name,
				      const char *issuer_common_name,
				      const char *not_before,
				      const char *not_after,
				      X509 &cert,
				      std::span<const std::byte> key,
				      const char *key_wrap_name)
{
	const SslBuffer cert_buffer(cert);
	const Pg::BinaryValue cert_der(cert_buffer.get());

	const Pg::BinaryValue key_der(key);

	InsertServerCertificate(handle, special,
				common_name, issuer_common_name,
				not_before, not_after,
				cert_der, key_der, key_wrap_name);
}

bool
CertDatabase::LoadServerCertificate(const char *handle, const char *special,
				    X509 &cert, const EVP_PKEY &key,
				    const char *key_wrap_name,
				    WrapKey *wrap_key)
{
	const auto common_name = GetCommonName(cert);
	assert(common_name != nullptr);

	const auto issuer_common_name = GetIssuerCommonName(cert);

	const SslBuffer cert_buffer(cert);
	const Pg::BinaryValue cert_der(cert_buffer.get());

	const SslBuffer key_buffer(key);
	Pg::BinaryValue key_der(key_buffer.get());

	AllocatedArray<std::byte> wrapped;
	if (key_wrap_name != nullptr)
		key_der = wrapped = wrap_key->Decrypt(key_der);

	const auto alt_names = GetSubjectAltNames(cert);

	const auto not_before = FormatTime(X509_get0_notBefore(&cert));
	if (not_before == nullptr)
		throw "Certificate does not have a notBefore time stamp";

	const auto not_after = FormatTime(X509_get0_notAfter(&cert));
	if (not_after == nullptr)
		throw "Certificate does not have a notAfter time stamp";

	auto result = UpdateServerCertificate(handle, special,
					      common_name.c_str(),
					      issuer_common_name.c_str(),
					      not_before.c_str(),
					      not_after.c_str(),
					      cert_der, key_der,
					      key_wrap_name);
	if (result.GetRowCount() > 0) {
		const char *id = result.GetValue(0, 0);
		DeleteAltNames(id);
		for (const auto &alt_name : alt_names)
			InsertAltName(id, alt_name.c_str());
		return false;
	} else {
		/* just in case a deleted certificate with the same name
		   already exists */
		ReallyDeleteServerCertificateByName(common_name.c_str());

		result = InsertServerCertificate(handle, special,
						 common_name.c_str(),
						 issuer_common_name.c_str(),
						 not_before.c_str(),
						 not_after.c_str(),
						 cert_der, key_der,
						 key_wrap_name);
		const char *id = result.GetValue(0, 0);
		for (const auto &alt_name : alt_names)
			InsertAltName(id, alt_name.c_str());
		return true;
	}
}

UniqueX509
CertDatabase::GetServerCertificateByHandle(const char *handle)
{
	auto result = FindServerCertificateByHandle(handle);
	if (result.GetRowCount() == 0)
		return nullptr;

	return LoadCertificate(result, 0, 0);
}

UniqueCertKey
CertDatabase::GetServerCertificateKeyByHandle(const char *handle)
{
	auto result = FindServerCertificateKeyByHandle(handle);
	if (result.GetRowCount() == 0)
		return {};

	return LoadCertificateKey(config, result, 0, 0);
}

UniqueCertKey
CertDatabase::GetServerCertificateKey(const char *name, const char *special)
{
	auto result = FindServerCertificateKeyByName(SyncQueryWrapper{conn},
						     name, special);
	if (result.GetRowCount() == 0) {
		/* no matching common_name; check for an altName */
		// TODO do both queries, use the most recent record
		result = FindServerCertificateKeyByAltName(SyncQueryWrapper{conn},
							   name, special);
		if (result.GetRowCount() == 0)
			return {};
	}

	return LoadCertificateKey(config, result, 0, 0);
}

UniqueCertKey
CertDatabase::GetServerCertificateKey(Pg::Serial id)
{
	auto result = FindServerCertificateKeyById(id);
	if (result.GetRowCount() == 0)
		return {};

	return LoadCertificateKey(config, result, 0, 0);
}

Pg::Result
CertDatabase::FindServerCertificatesByName(const char *name)
{
	return conn.ExecuteParams(false,
				  "SELECT id, handle, issuer_common_name, not_after "
				  "FROM server_certificate "
				  "WHERE NOT deleted AND "
				  "(common_name=$1 OR EXISTS("
				  "SELECT id FROM server_certificate_alt_name"
				  " WHERE server_certificate_id=server_certificate.id"
				  " AND name=$1))"
				  "ORDER BY"
				  " not_after DESC",
				  name);
}

std::forward_list<std::string>
CertDatabase::GetNamesByHandle(const char *handle)
{
	std::forward_list<std::string> names;
	auto i = names.before_begin();

	const char *sql = "SELECT common_name, "
		"ARRAY(SELECT name FROM server_certificate_alt_name WHERE server_certificate_id=server_certificate.id)"
		" FROM server_certificate"
		" WHERE handle=$1 AND NOT deleted";

	for (const auto &row : conn.ExecuteParams(sql, handle)) {
		i = names.emplace_after(i, row.GetValue(0));
		if (!row.IsValueNull(1)) {
			names.splice_after(i,
					   Pg::DecodeArray(row.GetValue(1)));
			while (std::next(i) != names.end())
				++i;
		}
	}

	return names;
}

void
CertDatabase::SetHandle(Pg::Serial id, const char *handle)
{
	auto result = conn.ExecuteParams("UPDATE server_certificate"
					 " SET handle=$2"
					 " WHERE id=$1",
					 id, handle);
	if (result.GetAffectedRows() < 1)
		throw std::runtime_error("No such record");
}


void
CertDatabase::InsertAcmeAccount(bool staging,
				const char *email, const char *location,
				EVP_PKEY &key, const char *key_wrap_name,
				WrapKey *wrap_key)
{
	const SslBuffer key_buffer(key);
	Pg::BinaryValue key_der(key_buffer.get());

	AllocatedArray<std::byte> wrapped;
	if (key_wrap_name != nullptr)
		key_der = wrapped = wrap_key->Encrypt(key_der);

	conn.ExecuteParams("INSERT INTO acme_account("
			   "staging, email, location, key_der, key_wrap_name) "
			   "VALUES($1, $2, $3, $4, $5)",
			   staging, email, location, key_der, key_wrap_name);
}

void
CertDatabase::TouchAcmeAccount(const char *id)
{
	conn.ExecuteParams("UPDATE acme_account SET time_used=now() WHERE id=$1",
			   id);
}

CertDatabase::AcmeAccount
CertDatabase::GetAcmeAccount(bool staging)
{
	const auto result = conn.ExecuteParams(true, R"(
SELECT id::varchar,location,key_der,key_wrap_name
FROM acme_account
WHERE enabled AND staging=$1
ORDER BY time_used NULLS FIRST
LIMIT 1)", staging);
	if (result.IsEmpty())
		throw std::runtime_error("No valid ACME account in database");

	const char *id = result.GetValue(0, 0);
	TouchAcmeAccount(id);

	return {id, result.GetValue(0, 1),
		LoadWrappedKey(config, result, 0, 2)};
}
