// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include "pg/Connection.hxx"
#include "pg/Transaction.hxx"
#include "lib/openssl/UniqueEVP.hxx"
#include "lib/openssl/UniqueX509.hxx"

#include <span>
#include <string>

struct CertDatabaseConfig;
struct UniqueCertKey;
class WrapKey;

class CertDatabase {
	const CertDatabaseConfig &config;

	Pg::Connection conn;

public:
	explicit CertDatabase(const CertDatabaseConfig &_config);

	ConnStatusType GetStatus() const {
		return conn.GetStatus();
	}

	[[gnu::pure]]
	const char *GetErrorMessage() const {
		return conn.GetErrorMessage();
	}

	bool CheckConnected();
	void EnsureConnected();

	[[gnu::pure]]
	int GetSocket() const {
		return conn.GetSocket();
	}

	void ConsumeInput() {
		conn.ConsumeInput();
	}

	Pg::Notify GetNextNotify() {
		return conn.GetNextNotify();
	}

	Pg::Result ListenModified();
	Pg::Result NotifyModified();

	[[gnu::pure]]
	std::string GetCurrentTimestamp() noexcept {
		try {
			const auto result = conn.Execute("SELECT CURRENT_TIMESTAMP");
			return result.GetOnlyStringChecked();
		} catch (...) {
			return {};
		}
	}

	[[gnu::pure]]
	std::string GetLastModified() noexcept {
		try {
			const auto result = conn.Execute("SELECT MAX(modified) FROM server_certificate");
			return result.GetOnlyStringChecked();
		} catch (...) {
			return {};
		}
	}

	template<typename F>
	void DoSerializable(F &&f) {
		conn.DoSerializable(std::forward<F>(f));
	}

	template<typename F>
	void DoSerializableRepeat(unsigned retries, F &&f) {
		Pg::DoSerializableRepeat(conn, retries, std::forward<F>(f));
	}

	template<typename F>
	void DoRepeatableRead(F &&f) {
		conn.DoRepeatableRead(std::forward<F>(f));
	}

	template<typename F>
	void DoRepeatableReadRepeat(unsigned retries, F &&f) {
		Pg::DoRepeatableReadRepeat(conn, retries, std::forward<F>(f));
	}

	void Migrate();

	Pg::Serial GetIdByHandle(const char *handle);

	void InsertServerCertificate(const char *handle,
				     const char *special,
				     const char *common_name,
				     const char *issuer_common_name,
				     const char *not_before,
				     const char *not_after,
				     X509 &cert,
				     std::span<const std::byte> key,
				     const char *key_wrap_name);

	/**
	 * @return true when new certificate has been inserted, false when an
	 * existing certificate has been updated
	 */
	bool LoadServerCertificate(const char *handle, const char *special,
				   X509 &cert, const EVP_PKEY &key,
				   const char *key_wrap_name,
				   WrapKey *wrap_key);

	UniqueX509 GetServerCertificateByHandle(const char *handle);

	/**
	 * Throws std::runtime_error on error.
	 *
	 * @return a pair of certificate and key, or {nullptr, nullptr} if
	 * no matching certificate was found
	 */
	UniqueCertKey GetServerCertificateKeyByHandle(const char *handle);
	UniqueCertKey GetServerCertificateKey(const char *name,
					      const char *special);
	UniqueCertKey GetServerCertificateKey(Pg::Serial id);

	/**
	 * Result columns: id, handle, issuer_common_name, not_after
	 */
	Pg::Result FindServerCertificatesByName(const char *name);

	std::forward_list<std::string> GetNamesByHandle(const char *handle);

	void SetHandle(Pg::Serial id, const char *handle);

private:
	Pg::Result InsertServerCertificate(const char *handle,
					   const char *special,
					   const char *common_name,
					   const char *issuer_common_name,
					   const char *not_before,
					   const char *not_after,
					   Pg::BinaryValue cert, Pg::BinaryValue key,
					   const char *key_wrap_name) {
		return conn.ExecuteParams("INSERT INTO server_certificate("
					  "handle, special, common_name, issuer_common_name, "
					  "not_before, not_after, "
					  "certificate_der, key_der, key_wrap_name) "
					  "VALUES($1, $2, $3, $4, $5, $6, $7, $8, $9)"
					  " RETURNING id",
					  handle, special,
					  common_name, issuer_common_name,
					  not_before, not_after,
					  cert, key, key_wrap_name);
	}

	Pg::Result UpdateServerCertificate(const char *handle, const char *special,
					   const char *common_name,
					   const char *issuer_common_name,
					   const char *not_before,
					   const char *not_after,
					   Pg::BinaryValue cert, Pg::BinaryValue key,
					   const char *key_wrap_name) {
		// TODO: remove handle==nullptr support eventually
		return conn.ExecuteParams(handle != nullptr
					  ? "UPDATE server_certificate SET "
					  "common_name=$1, "
					  "not_before=$2, not_after=$3, "
					  "certificate_der=$4, key_der=$5, "
					  "key_wrap_name=$6, "
					  "issuer_common_name=$7, "
					  "modified=CURRENT_TIMESTAMP, deleted=FALSE "
					  "WHERE handle=$8"
					  " AND special IS NOT DISTINCT FROM $9"
					  " RETURNING id"
					  : "UPDATE server_certificate SET "
					  "not_before=$2, not_after=$3, "
					  "certificate_der=$4, key_der=$5, "
					  "key_wrap_name=$6, "
					  "issuer_common_name=$7, "
					  "handle=$8, "
					  "modified=CURRENT_TIMESTAMP, deleted=FALSE "
					  "WHERE common_name=$1"
					  " AND special IS NOT DISTINCT FROM $9"
					  " RETURNING id",
					  common_name, not_before, not_after,
					  cert, key, key_wrap_name,
					  issuer_common_name, handle, special);
	}

	Pg::Result DeleteAltNames(const char *server_certificate_id) {
		return conn.ExecuteParams("DELETE FROM server_certificate_alt_name"
					  " WHERE server_certificate_id=$1",
					  server_certificate_id);
	}

	Pg::Result InsertAltName(const char *server_certificate_id,
				 const char *name) {
		return conn.ExecuteParams("INSERT INTO server_certificate_alt_name"
					  "(server_certificate_id, name)"
					  " VALUES($1, $2)",
					  server_certificate_id, name);
	}

public:
	Pg::Result DeleteServerCertificateByHandle(const char *handle) {
		return conn.ExecuteParams(true,
					  "UPDATE server_certificate SET "
					  "modified=CURRENT_TIMESTAMP, deleted=TRUE "
					  "WHERE handle=$1 AND NOT deleted",
					  handle);
	}

private:
	Pg::Result FindServerCertificateByHandle(const char *handle) {
		return conn.ExecuteParams(true,
					  "SELECT certificate_der "
					  "FROM server_certificate "
					  "WHERE NOT deleted AND handle=$1"
					  "LIMIT 1",
					  handle);
	}

	Pg::Result FindServerCertificateKeyByHandle(const char *handle) {
		return conn.ExecuteParams(true,
					  "SELECT certificate_der, key_der, key_wrap_name "
					  "FROM server_certificate "
					  "WHERE handle=$1 AND NOT deleted "
					  "LIMIT 1",
					  handle);
	}

	Pg::Result FindServerCertificateKeyById(Pg::Serial id) {
		return conn.ExecuteParams(true,
					  "SELECT certificate_der, key_der, key_wrap_name "
					  "FROM server_certificate "
					  "WHERE id=$1",
					  id);
	}

	/**
	 * Invoke SQL "DELETE" on the given certificate which has the
	 * "deleted" flag set.  This is used prior to "INSERT"ing a new
	 * certificate when an old deleted one with the same name may
	 * already exist.  Without a following INSERT, this is an unsafe
	 * operation, because it may break beng-lb's certificate cache.
	 */
	Pg::Result ReallyDeleteServerCertificateByName(const char *common_name) {
		return conn.ExecuteParams(true,
					  "DELETE FROM server_certificate "
					  "WHERE common_name=$1 AND deleted",
					  common_name);
	}

public:
	Pg::Result GetModifiedServerCertificatesMeta(const char *since) {
		return conn.ExecuteParams("SELECT deleted, modified, handle "
					  "FROM server_certificate "
					  "WHERE modified>$1",
					  since);
	}

	Pg::Result TailModifiedServerCertificatesMeta() {
		return conn.Execute("SELECT deleted, modified, handle "
				    "FROM server_certificate "
				    "ORDER BY modified DESC LIMIT 20");
	}

	void InsertAcmeAccount(bool staging,
			       const char *email, const char *location,
			       EVP_PKEY &key, const char *key_wrap_name,
			       WrapKey *wrap_key);

	void TouchAcmeAccount(const char *id);

	struct AcmeAccount {
		std::string id;

		std::string location;

		UniqueEVP_PKEY key;
	};

	AcmeAccount GetAcmeAccount(bool staging);
};
