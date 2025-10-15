// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Client.hxx"
#include "Config.hxx"
#include "Filter.hxx"
#include "AlpnProtos.hxx"
#include "Basic.hxx"
#include "lib/fmt/RuntimeError.hxx"
#include "lib/openssl/LoadFile.hxx"
#include "lib/openssl/Error.hxx"
#include "lib/openssl/UniqueCertKey.hxx"
#include "io/Logger.hxx"
#include "fs/ThreadSocketFilter.hxx"
#include "thread/Pool.hxx"

#include <map>

using std::string_view_literals::operator""sv;

class SslClientCerts {
	struct X509NameCompare {
		[[gnu::pure]]
		bool operator()(const UniqueX509_NAME &a,
				const UniqueX509_NAME &b) const noexcept {
			return X509_NAME_cmp(a.get(), b.get()) < 0;
		}
	};

	std::map<UniqueX509_NAME, UniqueCertKey,
		 X509NameCompare> by_issuer;

	std::map<std::string, UniqueCertKey> by_name;

public:
	explicit SslClientCerts(const std::vector<NamedSslCertKeyConfig> &config);

	bool Find(X509_NAME &name, X509 **x509, EVP_PKEY **pkey) const noexcept;

	[[gnu::pure]]
	const auto *FindByConfiguredName(const char *name) const {
		auto i = by_name.find(name);
		return i != by_name.end()
			? &i->second
			: nullptr;
	}
};

inline int
SslClientFactory::ClientCertCallback_(SSL *ssl, X509 **x509,
				      EVP_PKEY **pkey) noexcept
{
	assert(certs != nullptr);

	const auto cas = SSL_get_client_CA_list(ssl);
	if (cas == nullptr)
		return 0;

	for (unsigned i = 0, n = sk_X509_NAME_num(cas); i < n; ++i)
		if (certs->Find(*sk_X509_NAME_value(cas, i), x509, pkey))
			return 1;

	return 0;
}

int
SslClientFactory::ClientCertCallback(SSL *ssl, X509 **x509,
				     EVP_PKEY **pkey) noexcept
{
	return GetFactory(ssl).ClientCertCallback_(ssl, x509, pkey);
}

static auto
LoadCertKey(const SslCertKeyConfig &config)
{
	return LoadCertKeyFile(config.cert_file.c_str(), config.key_file.c_str());
}

SslClientCerts::SslClientCerts(const std::vector<NamedSslCertKeyConfig> &config)
{
	for (const auto &i : config) {
		try {
			auto ck = LoadCertKey(i);
			if (!i.name.empty()) {
				auto j = by_name.emplace(std::piecewise_construct,
							 std::forward_as_tuple(i.name),
							 std::forward_as_tuple(UpRef(ck)));
				if (!j.second)
					throw FmtRuntimeError("Duplicate certificate name {:?}"sv,
							      i.name);
			}

			X509_NAME *issuer = X509_get_issuer_name(ck.cert.get());
			if (issuer != nullptr) {
				UniqueX509_NAME issuer2(X509_NAME_dup(issuer));
				if (issuer2)
					by_issuer.emplace(std::move(issuer2),
							  std::move(ck));
			}
		} catch (...) {
			std::throw_with_nested(FmtRuntimeError("Failed to load certificate {:?}/{:?}"sv,
							       i.cert_file, i.key_file));
		}
	}
}

bool
SslClientCerts::Find(X509_NAME &name,
		     X509 **x509, EVP_PKEY **pkey) const noexcept
{
	UniqueX509_NAME name2(X509_NAME_dup(&name));
	if (!name2)
		return false;

	auto i = by_issuer.find(name2);
	if (i == by_issuer.end())
		return false;

	*x509 = UpRef(*i->second.cert).release();
	*pkey = UpRef(*i->second.key).release();
	return true;
}

SslClientFactory::SslClientFactory(const SslClientConfig &config)
	:ctx(CreateBasicSslCtx(false))
{
	if (idx < 0)
		idx = SSL_CTX_get_ex_new_index(0, NULL, NULL, NULL, NULL);

	SSL_CTX_set_ex_data(ctx.get(), idx, this);

	if (!config.cert_key.empty()) {
		certs = std::make_unique<SslClientCerts>(config.cert_key);
		SSL_CTX_set_client_cert_cb(ctx.get(), ClientCertCallback);
	}
}

SslClientFactory::~SslClientFactory() noexcept = default;

SocketFilterPtr
SslClientFactory::Create(EventLoop &event_loop,
			 const char *hostname,
			 const char *certificate,
			 SslClientAlpn alpn)
{
	UniqueSSL ssl(SSL_new(ctx.get()));
	if (!ssl)
		throw SslError("SSL_new() failed");

	switch (alpn) {
	case SslClientAlpn::NONE:
		break;

	case SslClientAlpn::HTTP_2:
		SSL_set_alpn_protos(ssl.get(), alpn_h2.data(), alpn_h2.size());
		break;

	case SslClientAlpn::HTTP_ANY:
		SSL_set_alpn_protos(ssl.get(), alpn_http_any.data(),
				    alpn_http_any.size());
		break;
	}

	SSL_set_connect_state(ssl.get());

	if (hostname != nullptr)
		/* why the fuck does OpenSSL want a non-const string? */
		SSL_set_tlsext_host_name(ssl.get(), const_cast<char *>(hostname));

	if (certificate != nullptr) {
		const auto *c = certs != nullptr
			? certs->FindByConfiguredName(certificate)
			: nullptr;
		if (c == nullptr)
			throw std::runtime_error("Selected certificate not found in configuration");

		SSL_use_PrivateKey(ssl.get(), c->key.get());
		SSL_use_certificate(ssl.get(), c->cert.get());
	}

	auto &queue = thread_pool_get_queue(event_loop);
	return SocketFilterPtr(new ThreadSocketFilter(queue,
						      ssl_filter_new(std::move(ssl))));
}
