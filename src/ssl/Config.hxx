// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#ifndef BENG_PROXY_SSL_CONFIG_H
#define BENG_PROXY_SSL_CONFIG_H

#include <string>
#include <vector>

enum class SslVerify {
	NO,
	YES,
	OPTIONAL,
};

struct SslCertKeyConfig {
	std::string cert_file;

	std::string key_file;

	template<typename C, typename K>
	SslCertKeyConfig(C &&_cert_file, K &&_key_file)
		:cert_file(std::forward<C>(_cert_file)),
		 key_file(std::forward<K>(_key_file)) {}
};

/**
 * SSL/TLS configuration.
 */
struct SslConfig {
	std::vector<SslCertKeyConfig> cert_key;

	std::string ca_cert_file;

	SslVerify verify = SslVerify::NO;
};

struct NamedSslCertKeyConfig : SslCertKeyConfig {
	std::string name;

	template<typename N, typename C, typename K>
	NamedSslCertKeyConfig(N &&_name, C &&_cert_file, K &&_key_file) noexcept
		:SslCertKeyConfig(std::forward<C>(_cert_file),
				  std::forward<K>(_key_file)),
		 name(std::forward<N>(_name)) {}
};

struct SslClientConfig {
	std::vector<NamedSslCertKeyConfig> cert_key;
};

#endif
