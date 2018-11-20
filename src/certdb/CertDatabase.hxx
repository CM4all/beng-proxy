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

#pragma once

#include "pg/Connection.hxx"
#include "pg/Transaction.hxx"
#include "ssl/Unique.hxx"

#include <string>

typedef struct aes_key_st AES_KEY;
struct CertDatabaseConfig;

class CertDatabase {
	const CertDatabaseConfig &config;

	Pg::Connection conn;

public:
	explicit CertDatabase(const CertDatabaseConfig &_config);

	ConnStatusType GetStatus() const {
		return conn.GetStatus();
	}

	gcc_pure
	const char *GetErrorMessage() const {
		return conn.GetErrorMessage();
	}

	bool CheckConnected();
	void EnsureConnected();

	gcc_pure
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

	gcc_pure
	std::string GetCurrentTimestamp() noexcept {
		try {
			const auto result = conn.Execute("SELECT CURRENT_TIMESTAMP");
			return result.GetOnlyStringChecked();
		} catch (...) {
			return {};
		}
	}

	gcc_pure
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
				     const char *common_name,
				     const char *issuer_common_name,
				     const char *not_before,
				     const char *not_after,
				     X509 &cert, ConstBuffer<void> key,
				     const char *key_wrap_name);

	/**
	 * @return true when new certificate has been inserted, false when an
	 * existing certificate has been updated
	 */
	bool LoadServerCertificate(const char *handle,
				   X509 &cert, EVP_PKEY &key,
				   const char *key_wrap_name,
				   AES_KEY *wrap_key);

	UniqueX509 GetServerCertificateByHandle(const char *handle);

	/**
	 * Throws std::runtime_error on error.
	 *
	 * @return a pair of certificate and key, or {nullptr, nullptr} if
	 * no matching certificate was found
	 */
	std::pair<UniqueX509, UniqueEVP_PKEY> GetServerCertificateKeyByHandle(const char *handle);
	std::pair<UniqueX509, UniqueEVP_PKEY> GetServerCertificateKey(const char *name);
	std::pair<UniqueX509, UniqueEVP_PKEY> GetServerCertificateKey(Pg::Serial id);

	/**
	 * Result columns: id, handle, issuer_common_name, not_after
	 */
	Pg::Result FindServerCertificatesByName(const char *name);

	std::forward_list<std::string> GetNamesByHandle(const char *handle);

	void SetHandle(Pg::Serial id, const char *handle);

private:
	Pg::Result InsertServerCertificate(const char *handle,
					   const char *common_name,
					   const char *issuer_common_name,
					   const char *not_before,
					   const char *not_after,
					   Pg::BinaryValue cert, Pg::BinaryValue key,
					   const char *key_wrap_name) {
		return conn.ExecuteBinary("INSERT INTO server_certificate("
					  "handle, common_name, issuer_common_name, "
					  "not_before, not_after, "
					  "certificate_der, key_der, key_wrap_name) "
					  "VALUES($1, $2, $3, $4, $5, $6, $7, $8)"
					  " RETURNING id",
					  handle, common_name, issuer_common_name,
					  not_before, not_after,
					  cert, key, key_wrap_name);
	}

	Pg::Result UpdateServerCertificate(const char *handle,
					   const char *common_name,
					   const char *issuer_common_name,
					   const char *not_before,
					   const char *not_after,
					   Pg::BinaryValue cert, Pg::BinaryValue key,
					   const char *key_wrap_name) {
		// TODO: remove handle==nullptr support eventually
		return conn.ExecuteBinary(handle != nullptr
					  ? "UPDATE server_certificate SET "
					  "common_name=$1, "
					  "not_before=$2, not_after=$3, "
					  "certificate_der=$4, key_der=$5, "
					  "key_wrap_name=$6, "
					  "issuer_common_name=$7, "
					  "modified=CURRENT_TIMESTAMP, deleted=FALSE "
					  "WHERE handle=$8"
					  " RETURNING id"
					  : "UPDATE server_certificate SET "
					  "not_before=$2, not_after=$3, "
					  "certificate_der=$4, key_der=$5, "
					  "key_wrap_name=$6, "
					  "issuer_common_name=$7, "
					  "handle=$8, "
					  "modified=CURRENT_TIMESTAMP, deleted=FALSE "
					  "WHERE common_name=$1"
					  " RETURNING id",
					  common_name, not_before, not_after,
					  cert, key, key_wrap_name,
					  issuer_common_name, handle);
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

	Pg::Result FindServerCertificateKeyByName(const char *common_name) {
		return conn.ExecuteParams(true,
					  "SELECT certificate_der, key_der, key_wrap_name "
					  "FROM server_certificate "
					  "WHERE NOT deleted AND "
					  "(common_name=$1 OR EXISTS("
					  "SELECT id FROM server_certificate_alt_name"
					  " WHERE server_certificate_id=server_certificate.id"
					  " AND name=$1))"
					  "ORDER BY"
					  /* prefer certificates which expire later */
					  " not_after DESC,"
					  /* prefer exact match in common_name: */
					  " common_name=$1 DESC "
					  "LIMIT 1",
					  common_name);
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
			       AES_KEY *wrap_key);

	void TouchAcmeAccount(const char *id);

	struct AcmeAccount {
		std::string id;

		std::string location;

		UniqueEVP_PKEY key;
	};

	AcmeAccount GetAcmeAccount(bool staging);
};
