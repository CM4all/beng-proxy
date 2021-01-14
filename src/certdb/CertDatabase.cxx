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

#include "CertDatabase.hxx"
#include "FromResult.hxx"
#include "Config.hxx"
#include "WrapKey.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Time.hxx"
#include "ssl/Name.hxx"
#include "ssl/AltName.hxx"
#include "io/FileDescriptor.hxx"
#include "util/AllocatedString.hxx"

#include <openssl/aes.h>

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
				      const char *common_name,
				      const char *issuer_common_name,
				      const char *not_before,
				      const char *not_after,
				      X509 &cert, ConstBuffer<void> key,
				      const char *key_wrap_name)
{
	const SslBuffer cert_buffer(cert);
	const Pg::BinaryValue cert_der(cert_buffer.get());

	const Pg::BinaryValue key_der(key);

	InsertServerCertificate(handle,
				common_name, issuer_common_name,
				not_before, not_after,
				cert_der, key_der, key_wrap_name);
}

bool
CertDatabase::LoadServerCertificate(const char *handle,
				    X509 &cert, EVP_PKEY &key,
				    const char *key_wrap_name,
				    AES_KEY *wrap_key)
{
	const auto common_name = GetCommonName(cert);
	assert(common_name != nullptr);

	const auto issuer_common_name = GetIssuerCommonName(cert);

	const SslBuffer cert_buffer(cert);
	const Pg::BinaryValue cert_der(cert_buffer.get());

	const SslBuffer key_buffer(key);
	Pg::BinaryValue key_der(key_buffer.get());

	std::unique_ptr<unsigned char[]> wrapped;
	if (wrap_key != nullptr)
		key_der = WapKey(key_der, wrap_key, wrapped);

	const auto alt_names = GetSubjectAltNames(cert);

	const auto not_before = FormatTime(X509_get_notBefore(&cert));
	if (not_before == nullptr)
		throw "Certificate does not have a notBefore time stamp";

	const auto not_after = FormatTime(X509_get_notAfter(&cert));
	if (not_after == nullptr)
		throw "Certificate does not have a notAfter time stamp";

	auto result = UpdateServerCertificate(handle,
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

		result = InsertServerCertificate(handle, common_name.c_str(),
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

std::pair<UniqueX509, UniqueEVP_PKEY>
CertDatabase::GetServerCertificateKeyByHandle(const char *handle)
{
	auto result = FindServerCertificateKeyByHandle(handle);
	if (result.GetRowCount() == 0)
		return std::make_pair(nullptr, nullptr);

	return LoadCertificateKey(config, result, 0, 0);
}

std::pair<UniqueX509, UniqueEVP_PKEY>
CertDatabase::GetServerCertificateKey(const char *name)
{
	auto result = FindServerCertificateKeyByName(name);
	if (result.GetRowCount() == 0)
		return std::make_pair(nullptr, nullptr);

	return LoadCertificateKey(config, result, 0, 0);
}

std::pair<UniqueX509, UniqueEVP_PKEY>
CertDatabase::GetServerCertificateKey(Pg::Serial id)
{
	auto result = FindServerCertificateKeyById(id);
	if (result.GetRowCount() == 0)
		return std::make_pair(nullptr, nullptr);

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
				AES_KEY *wrap_key)
{
	const SslBuffer key_buffer(key);
	Pg::BinaryValue key_der(key_buffer.get());

	std::unique_ptr<unsigned char[]> wrapped;
	if (wrap_key != nullptr)
		key_der = WapKey(key_der, wrap_key, wrapped);

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
