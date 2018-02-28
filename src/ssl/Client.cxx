/*
 * Copyright 2007-2018 Content Management AG
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

#include "Client.hxx"
#include "Config.hxx"
#include "Filter.hxx"
#include "ssl/Basic.hxx"
#include "ssl/Ctx.hxx"
#include "ssl/LoadFile.hxx"
#include "ssl/Error.hxx"
#include "io/Logger.hxx"
#include "fs/ThreadSocketFilter.hxx"
#include "thread_pool.hxx"
#include "util/RuntimeError.hxx"

#include <map>

class SslClientCerts {
    struct X509NameCompare {
        gcc_pure
        bool operator()(const UniqueX509_NAME &a,
                        const UniqueX509_NAME &b) const noexcept {
            return X509_NAME_cmp(a.get(), b.get()) < 0;
        }
    };

    std::map<UniqueX509_NAME,
             std::pair<UniqueX509, UniqueEVP_PKEY>,
             X509NameCompare> by_issuer;

public:
    explicit SslClientCerts(const std::vector<SslCertKeyConfig> &config);

    bool Find(X509_NAME &name, X509 **x509, EVP_PKEY **pkey) const noexcept;

};

static SslCtx ssl_client_ctx;
static SslClientCerts *ssl_client_certs;

static int
ssl_client_cert_cb(SSL *ssl, X509 **x509, EVP_PKEY **pkey) noexcept
{
    assert(ssl_client_certs != nullptr);

    const auto cas = SSL_get_client_CA_list(ssl);
    if (cas == nullptr)
        return 0;

    for (unsigned i = 0, n = sk_X509_NAME_num(cas); i < n; ++i)
        if (ssl_client_certs->Find(*sk_X509_NAME_value(cas, i), x509, pkey))
            return 1;

    return 0;
}

static auto
LoadCertKey(const SslCertKeyConfig &config)
{
    return LoadCertKeyFile(config.cert_file.c_str(), config.key_file.c_str());
}

SslClientCerts::SslClientCerts(const std::vector<SslCertKeyConfig> &config)
{
    for (const auto &i : config) {
        try {
            auto ck = LoadCertKey(i);
            X509_NAME *issuer = X509_get_issuer_name(ck.first.get());
            if (issuer != nullptr) {
                UniqueX509_NAME issuer2(X509_NAME_dup(issuer));
                if (issuer2)
                    by_issuer.emplace(std::move(issuer2),
                                      std::make_pair(std::move(ck.first),
                                                     std::move(ck.second)));
            }
        } catch (...) {
            std::throw_with_nested(FormatRuntimeError("Failed to load certificate '%s'/'%s'",
                                                      i.cert_file.c_str(),
                                                      i.key_file.c_str()));
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

    X509_up_ref(i->second.first.get());
    *x509 = i->second.first.get();

    EVP_PKEY_up_ref(i->second.second.get());
    *pkey = i->second.second.get();
    return true;
}

void
ssl_client_init(const SslClientConfig &config)
{
    try {
        ssl_client_ctx = CreateBasicSslCtx(false);
    } catch (const SslError &e) {
        LogConcat(1, "ssl_client", "ssl_factory_new() failed: ", e.what());
    }

    if (!config.cert_key.empty()) {
        ssl_client_certs = new SslClientCerts(config.cert_key);
        SSL_CTX_set_client_cert_cb(ssl_client_ctx.get(), ssl_client_cert_cb);
    }
}

void
ssl_client_deinit()
{
    ssl_client_ctx.reset();
    delete std::exchange(ssl_client_certs, nullptr);
}

SocketFilterPtr
ssl_client_create(EventLoop &event_loop,
                  const char *hostname)
{
    UniqueSSL ssl(SSL_new(ssl_client_ctx.get()));
    if (!ssl)
        throw SslError("SSL_new() failed");

    SSL_set_connect_state(ssl.get());

    if (hostname != nullptr)
        /* why the fuck does OpenSSL want a non-const string? */
        SSL_set_tlsext_host_name(ssl.get(), const_cast<char *>(hostname));

    auto f = ssl_filter_new(std::move(ssl));

    auto &queue = thread_pool_get_queue(event_loop);
    return SocketFilterPtr(new ThreadSocketFilter(event_loop, queue,
                                                  &ssl_filter_get_handler(*f)));
}
