/*
 * Copyright 2007-2017 Content Management AG
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

#include "Progress.hxx"
#include "AcmeUtil.hxx"
#include "AcmeError.hxx"
#include "AcmeClient.hxx"
#include "AcmeConfig.hxx"
#include "Config.hxx"
#include "CertDatabase.hxx"
#include "WrapKey.hxx"
#include "Wildcard.hxx"
#include "ssl/Init.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Dummy.hxx"
#include "ssl/Edit.hxx"
#include "ssl/Key.hxx"
#include "ssl/LoadFile.hxx"
#include "ssl/AltName.hxx"
#include "ssl/Name.hxx"
#include "ssl/GeneralName.hxx"
#include "ssl/MemBio.hxx"
#include "ssl/Unique.hxx"
#include "ssl/Error.hxx"
#include "pg/CheckError.hxx"
#include "lb/Config.hxx"
#include "system/urandom.hxx"
#include "system/Error.hxx"
#include "io/StringFile.hxx"
#include "util/ConstBuffer.hxx"
#include "util/PrintException.hxx"
#include "util/Compiler.h"

#include <json/json.h>

#include <thread>
#include <stdexcept>
#include <set>

#include <stdio.h>
#include <stdlib.h>
#include <poll.h>

struct Usage {
    const char *text;

    explicit Usage(const char *_text):text(_text) {}
};

struct AutoUsage {};

static const CertDatabaseConfig *db_config;

static WorkshopProgress root_progress;

static void
LoadCertificate(const char *handle,
                const char *cert_path, const char *key_path)
{
    const ScopeSslGlobalInit ssl_init;

    const auto cert = LoadCertFile(cert_path);
    const auto common_name = GetCommonName(*cert);
    if (common_name == nullptr)
        throw "Certificate has no common name";

    const auto key = LoadKeyFile(key_path);
    if (!MatchModulus(*cert, *key))
        throw "Key and certificate do not match.";

    WrapKeyHelper wrap_key_helper;
    const auto wrap_key = wrap_key_helper.SetEncryptKey(*db_config);

    CertDatabase db(*db_config);

    bool inserted;

    db.DoSerializableRepeat(8, [&](){
            inserted = db.LoadServerCertificate(handle,
                                                *cert, *key, wrap_key.first,
                                                wrap_key.second);
        });

    printf("%s: %s\n", inserted ? "insert" : "update", common_name.c_str());
    db.NotifyModified();
}

static void
ReloadCertificate(const char *handle)
{
    const ScopeSslGlobalInit ssl_init;

    CertDatabase db(*db_config);

    auto cert_key = db.GetServerCertificateKeyByHandle(handle);
    if (!cert_key.second)
        throw "Certificate not found";

    WrapKeyHelper wrap_key_helper;
    const auto wrap_key = wrap_key_helper.SetEncryptKey(*db_config);

    db.LoadServerCertificate(handle,
                             *cert_key.first, *cert_key.second,
                             wrap_key.first, wrap_key.second);
}

static void
DeleteCertificate(const char *handle)
{
    CertDatabase db(*db_config);

    const auto result = CheckError(db.DeleteServerCertificateByHandle(handle));
    if (result.GetAffectedRows() == 0)
        throw "Certificate not found";

    db.NotifyModified();
}

static void
GetCertificate(const char *handle)
{
    const ScopeSslGlobalInit ssl_init;
    CertDatabase db(*db_config);
    auto cert = db.GetServerCertificateByHandle(handle);
    if (!cert)
        throw "Certificate not found";

    X509_print_fp(stdout, cert.get());
    PEM_write_X509(stdout, cert.get());
}

/**
 * Load the private key for the given host name from the database.
 *
 * Returns the key or nullptr if no such certificate/key pair was
 * found.  Throws an exception on error.
 */
static UniqueEVP_PKEY
FindKeyByName(CertDatabase &db, const char *common_name)
{
    return db.GetServerCertificateKey(common_name).second;
}

static void
FindPrintCertificates(CertDatabase &db, const char *name)
{
    for (const auto &row : CheckError(db.FindServerCertificatesByName(name)))
        printf("%s\t%s\t%s\t%s\n",
               row.GetValue(0), row.GetValue(1),
               row.GetValue(2), row.GetValue(3));
}

static void
FindCertificate(const char *host, bool headers)
{
    if (headers)
        printf("id\thandle\tissuer\tnot_after\n");

    const ScopeSslGlobalInit ssl_init;
    CertDatabase db(*db_config);

    FindPrintCertificates(db, host);

    const auto wildcard = MakeCommonNameWildcard(host);
    if (!wildcard.empty())
        FindPrintCertificates(db, wildcard.c_str());
}

static void
DumpKey(const char *host)
{
    const ScopeSslGlobalInit ssl_init;
    CertDatabase db(*db_config);

    auto key = FindKeyByName(db, host);
    if (!key)
        throw "Key not found";

    if (PEM_write_PrivateKey(stdout, key.get(), nullptr, nullptr, 0,
                             nullptr, nullptr) <= 0)
        throw SslError("Failed to dump key");
}

gcc_noreturn
static void
Monitor()
{
    CertDatabase db(*db_config);
    CheckError(db.ListenModified());

    std::string last_modified = db.GetLastModified();
    if (last_modified.empty()) {
        last_modified = db.GetCurrentTimestamp();
        if (last_modified.empty())
            throw "CURRENT_TIMESTAMP failed";
    }

    struct pollfd pfd = {
        .fd = db.GetSocket(),
        .events = POLLIN,
    };

    while (true) {
        if (poll(&pfd, 1, -1) < 0)
            throw "poll() failed";

        db.ConsumeInput();
        while (db.GetNextNotify()) {}

        std::string new_last_modified = db.GetLastModified();
        if (new_last_modified.empty())
            throw "No MAX(modified) found";

        for (auto &row : CheckError(db.GetModifiedServerCertificatesMeta(last_modified.c_str())))
            printf("%s %s %s\n",
                   row.GetValue(1),
                   *row.GetValue(0) == 't' ? "deleted" : "modified",
                   row.GetValue(2));

        last_modified = std::move(new_last_modified);
    }
}

static void
Tail()
{
    CertDatabase db(*db_config);

    for (auto &row : CheckError(db.TailModifiedServerCertificatesMeta()))
        printf("%s %s %s\n",
               row.GetValue(1),
               *row.GetValue(0) == 't' ? "deleted" : "modified",
               row.GetValue(2));
}

static void
CopyCommonName(X509_REQ &req, X509 &src)
{
    X509_NAME *src_subject = X509_get_subject_name(&src);
    if (src_subject == nullptr)
        return;

    int i = X509_NAME_get_index_by_NID(src_subject, NID_commonName, -1);
    if (i < 0)
        return;

    auto *common_name = X509_NAME_get_entry(src_subject, i);
    auto *dest_subject = X509_REQ_get_subject_name(&req);
    X509_NAME_add_entry(dest_subject, common_name, -1, 0);
}

/**
 * Add a subject_alt_name extension for each host name in the list.
 */
template<typename L>
static void
AddDnsAltNames(X509_REQ &req, const L &hosts)
{
    OpenSSL::UniqueGeneralNames ns;
    for (const auto &host : hosts)
        ns.push_back(OpenSSL::ToDnsName(host));

    AddAltNames(req, ns);
}

/**
 * Copy the subject_alt_name extension from the source certificate to
 * the request.
 */
static void
CopyDnsAltNames(X509_REQ &req, X509 &src)
{
    int i = X509_get_ext_by_NID(&src, NID_subject_alt_name, -1);
    if (i < 0)
        /* no subject_alt_name found, no-op */
        return;

    auto ext = X509_get_ext(&src, i);
    if (ext == nullptr)
        return;

    OpenSSL::UniqueGeneralNames gn(reinterpret_cast<GENERAL_NAMES *>(X509V3_EXT_d2i(ext)));
    if (!gn)
        return;

    AddAltNames(req, gn);
}

static std::string
MakeHandleFromAcmeSni01(const std::string &acme)
{
    auto i = acme.find('.');
    if (i != 32)
        i = std::min<size_t>(32, acme.length());

    return "acme.invalid:" + acme.substr(0, i);
}

static UniqueX509
MakeTlsSni01Cert(EVP_PKEY &account_key, EVP_PKEY &key,
                 const AcmeClient::AuthzTlsSni01 &authz)
{
    const auto alt_host = authz.MakeDnsName(account_key);
    std::string alt_name = "DNS:" + alt_host;

    const std::string common_name = MakeHandleFromAcmeSni01(alt_host);

    auto cert = MakeSelfIssuedDummyCert(common_name.c_str());

    AddExt(*cert, NID_subject_alt_name, alt_name.c_str());

    X509_set_pubkey(cert.get(), &key);
    if (!X509_sign(cert.get(), &key, EVP_sha1()))
        throw SslError("X509_sign() failed");

    return cert;
}

static UniqueX509_REQ
MakeCertRequest(EVP_PKEY &key, const char *common_name,
                ConstBuffer<const char *> alt_hosts)
{
    UniqueX509_REQ req(X509_REQ_new());
    if (req == nullptr)
        throw "X509_REQ_new() failed";

    auto *name = X509_REQ_get_subject_name(req.get());

    if (!X509_NAME_add_entry_by_NID(name, NID_commonName, MBSTRING_ASC,
                                    const_cast<unsigned char *>((const unsigned char *)common_name),
                                    -1, -1, 0))
        throw SslError("X509_NAME_add_entry_by_NID() failed");

    if (!alt_hosts.empty())
        AddDnsAltNames(*req, alt_hosts);

    X509_REQ_set_pubkey(req.get(), &key);

    if (!X509_REQ_sign(req.get(), &key, EVP_sha1()))
        throw SslError("X509_REQ_sign() failed");

    return req;
}

static UniqueX509_REQ
MakeCertRequest(EVP_PKEY &key, X509 &src)
{
    UniqueX509_REQ req(X509_REQ_new());
    if (req == nullptr)
        throw "X509_REQ_new() failed";

    CopyCommonName(*req, src);
    CopyDnsAltNames(*req, src);

    X509_REQ_set_pubkey(req.get(), &key);

    if (!X509_REQ_sign(req.get(), &key, EVP_sha1()))
        throw SslError("X509_REQ_sign() failed");

    return req;
}

/**
 * Generate a tls-sni-01 challenge certificate, load it into the
 * database and send a PostgreSQL notify.  After returning, it is not
 * guaranteed that all servers have already updated their certificate
 * cache.
 *
 * @param key the ACME account key
 * @param cert_key a key for the new challenge certificate
 * @param authz_response the "new-authz" response from the ACME server
 * (i.e. the challenge)
 * @return the handle
 */
static AllocatedString<>
LoadAcmeNewAuthzChallenge(EVP_PKEY &key, CertDatabase &db,
                          EVP_PKEY &cert_key,
                          const AcmeClient::AuthzTlsSni01 &authz_response)
{
    const auto cert = MakeTlsSni01Cert(key, cert_key, authz_response);
    auto handle = GetCommonName(*cert);
    assert(!handle.IsNull());

    WrapKeyHelper wrap_key_helper;
    const auto wrap_key = wrap_key_helper.SetEncryptKey(*db_config);

    db.LoadServerCertificate(handle.c_str(), *cert, cert_key,
                             wrap_key.first, wrap_key.second);
    db.NotifyModified();

    return handle;
}

static void
HandleAcmeNewAuthz(EVP_PKEY &key, CertDatabase &db, AcmeClient &client,
                   EVP_PKEY &cert_key,
                   const AcmeClient::AuthzTlsSni01 &authz_response,
                   std::chrono::steady_clock::duration delay)
{
    const auto handle =
        LoadAcmeNewAuthzChallenge(key, db, cert_key, authz_response);
    assert(!handle.IsNull());

    printf("Loaded challenge certificate into database\n");

    /* wait until beng-lb's NameCache has been updated */
    if (!client.IsFake())
        std::this_thread::sleep_for(delay);

    printf("Waiting for confirmation from ACME server\n");
    bool done = client.UpdateAuthz(key, authz_response);
    while (!done) {
        std::this_thread::sleep_for(std::chrono::milliseconds(100));
        done = client.CheckAuthz(authz_response);
    }

    /* delete the challenge record */

    db.DeleteServerCertificateByHandle(handle.c_str());

    // TODO: delete orphaned challenge records
}

static void
AcmeNewAuthz(EVP_PKEY &key, CertDatabase &db, AcmeClient &client,
             const char *host)
{
    auto response = client.NewAuthz(key, host);
    const auto cert_key = GenerateRsaKey();

    /* 500ms is an arbitrary delay, somewhat bigger than NameCache's
       200ms delay */
    std::chrono::steady_clock::duration delay = std::chrono::milliseconds(500);

    unsigned unauthorized_retries = 3;

    while (true) {
        response = client.NewAuthz(key, host);

        try {
            HandleAcmeNewAuthz(key, db, client, *cert_key,
                               response, delay);
            break;
        } catch (...) {
            if (IsAcmeUnauthorizedError(std::current_exception()) &&
                unauthorized_retries-- > 0) {
                /* just in case this was caused by a timing problem
                   (the beng-lb instance had not updated its cache
                   yet), retry with a larger delay */

                PrintException(std::current_exception());
                fprintf(stderr, "Retrying new-authz.\n");
                delay *= 2;
                continue;
            }

            throw;
        }
    }
}

static void
AcmeNewCert(EVP_PKEY &key, CertDatabase &db, AcmeClient &client,
            const char *handle,
            EVP_PKEY &cert_key, X509_REQ &req)
{
    const auto cert = client.NewCert(key, req);

    WrapKeyHelper wrap_key_helper;
    const auto wrap_key = wrap_key_helper.SetEncryptKey(*db_config);

    db.DoSerializableRepeat(8, [&](){
            db.LoadServerCertificate(handle, *cert, cert_key,
                                     wrap_key.first, wrap_key.second);
        });

    db.NotifyModified();
}

static void
AcmeNewCert(EVP_PKEY &key, CertDatabase &db, AcmeClient &client,
            const char *handle,
            const char *host, ConstBuffer<const char *> alt_hosts)
{
    const auto cert_key = GenerateRsaKey();
    const auto req = MakeCertRequest(*cert_key, host, alt_hosts);
    AcmeNewCert(key, db, client, handle, *cert_key, *req);
}

static void
AcmeNewCertAll(EVP_PKEY &key, CertDatabase &db, AcmeClient &client,
               const char *handle,
               X509 &old_cert, EVP_PKEY &cert_key)
{
    const auto req = MakeCertRequest(cert_key, old_cert);
    AcmeNewCert(key, db, client, handle, cert_key, *req);
}

static void
AcmeNewAuthzCert(EVP_PKEY &key, CertDatabase &db, AcmeClient &client,
                 WorkshopProgress _progress,
                 const char *handle,
                 const char *host, ConstBuffer<const char *> alt_hosts)
{
    StepProgress progress(_progress, 1 + alt_hosts.size + 1);

    AcmeNewAuthz(key, db, client, host);
    progress();

    for (const auto *alt_host : alt_hosts) {
        AcmeNewAuthz(key, db, client, alt_host);
        progress();
    }

    AcmeNewCert(key, db, client, handle, host, alt_hosts);
    progress();
}

static std::set<std::string>
AllNames(X509 &cert)
{
    std::set<std::string> result;

    for (auto &i : GetSubjectAltNames(cert))
        if (!IsAcmeInvalid(i))
            /* ignore "*.acme.invalid" */
            result.emplace(std::move(i));

    const auto cn = GetCommonName(cert);
    if (!cn.IsNull())
        result.emplace(cn.c_str());

    return result;
}

static void
AcmeRenewCert(EVP_PKEY &key, CertDatabase &db, AcmeClient &client,
              WorkshopProgress _progress,
              const char *handle)
{
    const auto old_cert_key = db.GetServerCertificateKeyByHandle(handle);
    if (!old_cert_key.second)
        throw "Old certificate not found in database";

    auto &old_cert = *old_cert_key.first;
    auto &old_key = *old_cert_key.second;

    const auto cn = GetCommonName(old_cert);
    if (cn.IsNull())
        throw "Old certificate has no common name";

    const auto names = AllNames(old_cert);
    StepProgress progress(_progress, names.size() + 1);

    for (const auto &i : names) {
        printf("new-authz '%s'\n", i.c_str());
        AcmeNewAuthz(key, db, client, i.c_str());
        progress();
    }

    printf("new-cert\n");
    AcmeNewCertAll(key, db, client, handle, old_cert, old_key);
    progress();
}

static void
Acme(ConstBuffer<const char *> args)
{
    AcmeConfig config;

    while (!args.empty() && args.front()[0] == '-') {
        const char *arg = args.front();

        if (strcmp(arg, "--staging") == 0) {
            args.shift();
            config.staging = true;
        } else if (strcmp(arg, "--fake") == 0) {
            /* undocumented debugging option: no HTTP requests, fake
               ACME responses */
            args.shift();
            config.fake = true;
        } else if (strcmp(arg, "--agreement") == 0) {
            args.shift();

            if (args.empty())
                throw std::runtime_error("Agreement URL missing");

            config.agreement_url = args.front();
            args.shift();
        } else
            break;
    }

    if (args.empty())
        throw "acme commands:\n"
            "  new-reg EMAIL\n"
            "  new-authz HOST\n"
            "  new-cert HANDLE HOST...\n"
            "  new-authz-cert HANDLE HOST...\n"
            "  renew-cert HANDLE\n"
            "\n"
            "options:\n"
            "  --staging     use the Let's Encrypt staging server\n"
            "  --agreement URL\n"
            "                use a custom ACME agreement URL\n";

    const char *key_path = "/etc/cm4all/acme/account.key";

    const auto cmd = args.shift();

    if (strcmp(cmd, "new-reg") == 0) {
        if (args.size != 1)
            throw Usage("acme new-reg EMAIL");

        const char *email = args[0];

        const ScopeSslGlobalInit ssl_init;

        const auto key = LoadKeyFile(key_path);
        if (EVP_PKEY_base_id(key.get()) != EVP_PKEY_RSA)
            throw "RSA key expected";

        const auto account = AcmeClient(config).NewReg(*key, email);
        printf("location: %s\n", account.location.c_str());
    } else if (strcmp(cmd, "new-authz") == 0) {
        if (args.size != 1)
            throw Usage("acme new-authz HOST");

        const char *host = args[0];

        const ScopeSslGlobalInit ssl_init;

        const auto key = LoadKeyFile(key_path);
        if (EVP_PKEY_base_id(key.get()) != EVP_PKEY_RSA)
            throw "RSA key expected";

        CertDatabase db(*db_config);
        AcmeClient client(config);

        AcmeNewAuthz(*key, db, client, host);
        printf("OK\n");
    } else if (strcmp(cmd, "new-cert") == 0) {
        if (args.size < 2)
            throw Usage("acme new-cert HANDLE HOST...");

        const char *handle = args.shift();
        const char *host = args.shift();

        const ScopeSslGlobalInit ssl_init;

        const auto key = LoadKeyFile(key_path);
        if (EVP_PKEY_base_id(key.get()) != EVP_PKEY_RSA)
            throw "RSA key expected";

        CertDatabase db(*db_config);
        AcmeClient client(config);

        AcmeNewCert(*key, db, client, handle, host, args);

        printf("OK\n");
    } else if (strcmp(cmd, "new-authz-cert") == 0) {
        if (args.size < 2)
            throw Usage("acme new-authz-cert HANDLE HOST ...");

        const char *handle = args.shift();
        const char *host = args.shift();

        const ScopeSslGlobalInit ssl_init;

        const auto key = LoadKeyFile(key_path);
        if (EVP_PKEY_base_id(key.get()) != EVP_PKEY_RSA)
            throw "RSA key expected";

        CertDatabase db(*db_config);
        AcmeClient client(config);

        AcmeNewAuthzCert(*key, db, client, root_progress,
                         handle, host, args);

        printf("OK\n");
    } else if (strcmp(cmd, "renew-cert") == 0) {
        if (args.size != 1)
            throw Usage("acme renew-cert HANDLE");

        const char *handle = args.front();

        const ScopeSslGlobalInit ssl_init;

        const auto key = LoadKeyFile(key_path);
        if (EVP_PKEY_base_id(key.get()) != EVP_PKEY_RSA)
            throw "RSA key expected";

        CertDatabase db(*db_config);
        AcmeClient client(config);

        AcmeRenewCert(*key, db, client, root_progress, handle);

        printf("OK\n");
    } else
        throw "Unknown acme command";
}

static void
Populate(CertDatabase &db, EVP_PKEY *key, ConstBuffer<void> key_der,
         const char *common_name)
{
    (void)key;

    // TODO: fake time stamps
    const char *not_before = "1971-01-01";
    const char *not_after = "1971-01-01";

    auto cert = MakeSelfSignedDummyCert(*key, common_name);
    db.InsertServerCertificate(nullptr, common_name, common_name,
                               not_before, not_after,
                               *cert, key_der,
                               nullptr);
}

static void
Populate(const char *key_path, const char *suffix, unsigned n)
{
    const ScopeSslGlobalInit ssl_init;

    const auto key = LoadKeyFile(key_path);

    const SslBuffer key_buffer(*key);

    CertDatabase db(*db_config);

    if (n == 0) {
        Populate(db, key.get(), key_buffer.get(), suffix);
    } else {
        db.DoSerializableRepeat(2, [&](){
                for (unsigned i = 1; i <= n; ++i) {
                    char buffer[256];
                    snprintf(buffer, sizeof(buffer), "%u%s", i, suffix);
                    Populate(db, key.get(), key_buffer.get(), buffer);
                }
            });
    }

    db.NotifyModified();
}

static CertDatabaseConfig
LoadCertDatabaseConfig(const char *path)
{
    LbConfig lb_config;
    LoadConfigFile(lb_config, path);

    auto i = lb_config.cert_dbs.begin();
    if (i == lb_config.cert_dbs.end())
        throw "/etc/cm4all/beng/lb.conf contains no cert_db section";

    if (std::next(i) != lb_config.cert_dbs.end())
        fprintf(stderr, "Warning: %s contains multiple cert_db sections\n",
                path);

    return std::move(i->second);
}

static CertDatabaseConfig
LoadCertDatabaseConfig()
{
    return LoadCertDatabaseConfig("/etc/cm4all/beng/lb.conf");
}

/**
 * Load the "cert_db" section from "/etc/cm4all/beng/lb.conf", and
 * allow overriding the "connect" value from
 * "/etc/cm4all/beng/certdb.connect".
 */
static CertDatabaseConfig
LoadPatchCertDatabaseConfig()
{
    CertDatabaseConfig config = LoadCertDatabaseConfig();

    try {
        config.connect = LoadStringFile("/etc/cm4all/beng/certdb.connect");
    } catch (const std::system_error &e) {
        /* ignore ENOENT */
        if (!IsFileNotFound(e))
            throw;
    }

    return config;
}

static void
HandleLoad(ConstBuffer<const char *> args)
{
    if (args.size != 3)
        throw AutoUsage();

    LoadCertificate(args[0], args[1], args[2]);
}

static void
HandleReload(ConstBuffer<const char *> args)
{
    if (args.size != 1)
        throw AutoUsage();

    ReloadCertificate(args[0]);
}

static void
HandleDelete(ConstBuffer<const char *> args)
{
    if (args.size != 1)
        throw AutoUsage();

    DeleteCertificate(args[0]);
}

static void
PrintNames(const char *handle)
{
    CertDatabase db(*db_config);
    for (const auto &name : db.GetNamesByHandle(handle))
        printf("%s\n", name.c_str());
}

static void
HandleNames(ConstBuffer<const char *> args)
{
    if (args.size != 1)
        throw AutoUsage();

    PrintNames(args[0]);
}

static void
HandleGet(ConstBuffer<const char *> args)
{
    if (args.size != 1)
        throw AutoUsage();

    GetCertificate(args[0]);
}

static void
HandleFind(ConstBuffer<const char *> args)
{
    bool headers = false;

    while (!args.empty() && args.front()[0] == '-') {
        const char *arg = args.front();

        if (strcmp(arg, "--headers") == 0) {
            args.shift();
            headers = true;
        } else
            break;
    }

    if (args.size != 1)
        throw AutoUsage();

    FindCertificate(args[0], headers);
}

static void
SetHandle(Pg::Serial id, const char *handle)
{
    CertDatabase db(*db_config);
    db.SetHandle(id, handle);
}

static void
HandleSetHandle(ConstBuffer<const char *> args)
{
    if (args.size != 2)
        throw AutoUsage();

    SetHandle(Pg::Serial::Parse(args[0]), args[1]);
}

static void
HandleDumpKey(ConstBuffer<const char *> args)
{
    if (args.size != 1)
        throw AutoUsage();

    DumpKey(args[0]);
}

gcc_noreturn
static void
HandleMonitor(ConstBuffer<const char *> args)
{
    if (args.size != 0)
        throw AutoUsage();

    Monitor();
}

static void
HandleTail(ConstBuffer<const char *> args)
{
    if (args.size != 0)
        throw AutoUsage();

    Tail();
}

static void
HandleAcme(ConstBuffer<const char *> args)
{
    Acme(args);
}

static void
HandleGenwrap(ConstBuffer<const char *> args)
{
    if (args.size != 0)
        throw AutoUsage();

    CertDatabaseConfig::AES256 key;
    UrandomFill(&key, sizeof(key));

    for (auto b : key)
        printf("%02x", b);
    printf("\n");
}

static void
HandlePopulate(ConstBuffer<const char *> args)
{
    if (args.size < 2 || args.size > 3)
        throw AutoUsage();

    const char *key = args[0];
    const char *suffix = args[1];
    unsigned count = 0;

    if (args.size == 3) {
        count = strtoul(args[2], nullptr, 10);
        if (count == 0)
            throw std::runtime_error("Invalid COUNT parameter");
    }

    Populate(key, suffix, count);
}

static void
HandleMigrate(ConstBuffer<const char *> args)
{
    if (args.size != 0)
        throw AutoUsage();

    CertDatabase db(*db_config);
    db.Migrate();
}

static
#if GCC_OLDER_THAN(5,0)
/* for some reason, GCC 4.9 complains "error: array must be
   initialized with a brace-enclosed initializer" with "constexpr" */
const
#else
constexpr
#endif
struct Command {
    const char *name, *usage;
    void (*function)(ConstBuffer<const char *> args);
    bool undocumented = false;

    constexpr Command(const char *_name, const char *_usage,
                      void (*_function)(ConstBuffer<const char *> args),
                      bool _undocumented = false)
       :name(_name), usage(_usage),
        function(_function), undocumented(_undocumented) {}

} commands[] = {
    { "load", "HANDLE CERT KEY", HandleLoad },
    { "reload", "HANDLE", HandleReload, true },
    { "delete", "HANDLE", HandleDelete },
    { "names", "HANDLE", HandleNames },
    { "get", "HANDLE", HandleGet },
    { "find", "[--headers] HOST", HandleFind },
    { "set-handle", "ID HANDLE", HandleSetHandle },
    { "dumpkey", "HOST", HandleDumpKey, true },
    { "monitor", nullptr, HandleMonitor },
    { "tail", nullptr, HandleTail },
    { "acme", "[OPTIONS] COMMAND ...", HandleAcme },
    { "genwrap", "", HandleGenwrap },
    { "populate", "KEY SUFFIX COUNT", HandlePopulate, true },
    { "migrate", nullptr, HandleMigrate },
};

static const Command *
FindCommand(const char *name)
{
    for (const auto &i : commands)
        if (strcmp(i.name, name) == 0)
            return &i;

    return nullptr;
}

int
main(int argc, char **argv)
try {
    ConstBuffer<const char *> args(argv + 1, argc - 1);

    while (!args.empty() && *args.front() == '-') {
        if (strcmp(args.front(), "--progress") == 0) {
            args.shift();
            root_progress = WorkshopProgress(0, 100);
        } else if (strncmp(args.front(), "--progress=", 11) == 0) {
            const char *range = args.front() + 11;
            args.shift();

            char *endptr;
            unsigned min = strtoul(range, &endptr, 10);
            if (endptr == range || *endptr != '-' || min > 100)
                throw "Failed to parse progress range";

            range = endptr + 1;
            unsigned max = strtoul(range, &endptr, 10);
            if (endptr == range || *endptr != 0 || max < min || max > 100)
                throw "Failed to parse progress range";

            root_progress = WorkshopProgress(min, max);
        } else {
            fprintf(stderr, "Unknown option: %s\n\n", args.front());
            /* clear the list to trigger printing the usage */
            args.size = 0;
        }
    }

    if (args.empty()) {
        fprintf(stderr, "Usage: %s [OPTIONS] COMMAND ...\n"
                "\n"
                "Commands:\n", argv[0]);

        for (const auto &i : commands) {
            if (i.undocumented)
                continue;

            if (i.usage != nullptr)
                fprintf(stderr, "  %s %s\n", i.name, i.usage);
            else
                fprintf(stderr, "  %s\n", i.name);
        }

        fprintf(stderr, "\n"
                "Global options:\n"
                "  --progress[=MIN,MAX]  print Workshop job progress\n");

        return EXIT_FAILURE;
    }

    /* force line buffering, because this program may be used
       non-interactively, and mixing stdout/stderr is confusing in
       block-buffered mode */
    setvbuf(stdout, nullptr, _IOLBF, 0);
    setvbuf(stderr, nullptr, _IOLBF, 0);

    const auto cmd = args.shift();

    const auto _db_config = LoadPatchCertDatabaseConfig();
    db_config = &_db_config;

    const auto *cmd2 = FindCommand(cmd);
    if (cmd2 == nullptr) {
        fprintf(stderr, "Unknown command: %s\n", cmd);
        return EXIT_FAILURE;
    }

    try {
        cmd2->function(args);
    } catch (AutoUsage) {
        if (cmd2->usage != nullptr)
            fprintf(stderr, "Usage: %s %s %s\n", argv[0],
                    cmd2->name, cmd2->usage);
        else
            fprintf(stderr, "Usage: %s %s\n", argv[0],
                    cmd2->name);
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
} catch (const std::exception &e) {
    PrintException(e);
    return EXIT_FAILURE;
} catch (Usage u) {
    fprintf(stderr, "Usage: %s %s\n", argv[0], u.text);
    return EXIT_FAILURE;
} catch (const char *msg) {
    fprintf(stderr, "%s\n", msg);
    return EXIT_FAILURE;
}
