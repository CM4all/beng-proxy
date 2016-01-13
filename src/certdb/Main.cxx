#include "AcmeClient.hxx"
#include "Config.hxx"
#include "CertDatabase.hxx"
#include "Wildcard.hxx"
#include "ssl/ssl_init.hxx"
#include "ssl/Buffer.hxx"
#include "ssl/Util.hxx"
#include "ssl/AltName.hxx"
#include "ssl/Name.hxx"
#include "ssl/MemBio.hxx"
#include "ssl/Unique.hxx"
#include "ssl/Error.hxx"
#include "pg/Error.hxx"
#include "lb_config.hxx"
#include "RootPool.hxx"
#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

#include <openssl/ts.h>
#include <json/json.h>

#include <stdexcept>

#include <stdio.h>
#include <stdlib.h>
#include <poll.h>

static const CertDatabaseConfig *db_config;

static UniqueX509
LoadCertFile(const char *path)
{
    auto cert = TS_CONF_load_cert(path);
    if (cert == nullptr)
        throw SslError("Failed to load certificate");

    return UniqueX509(cert);
}

static UniqueEVP_PKEY
LoadKeyFile(const char *path)
{
    auto key = TS_CONF_load_key(path, nullptr);
    if (key == nullptr)
        throw SslError("Failed to load key");

    return UniqueEVP_PKEY(key);
}

static PgResult
CheckError(PgResult &&result)
{
    if (result.IsError())
        throw PgError(std::move(result));

    return std::move(result);
}

gcc_pure
static AllocatedString<>
GetCommonName(X509_NAME &name)
{
    return NidToString(name, NID_commonName);
}

gcc_pure
static AllocatedString<>
GetCommonName(X509 *cert)
{
    X509_NAME *subject = X509_get_subject_name(cert);
    return subject != nullptr
        ? GetCommonName(*subject)
        : nullptr;
}

gcc_pure
static AllocatedString<>
FormatTime(ASN1_TIME *t)
{
    if (t == nullptr)
        return nullptr;

    return BioWriterToString([t](BIO &bio){
            ASN1_TIME_print(&bio, t);
        });
}

/**
 * @return true when new certificate has been inserted, false when an
 * existing certificate has been updated
 */
static bool
LoadCertificate(CertDatabase &db, X509 &cert, EVP_PKEY &key)
{
    const auto common_name = GetCommonName(&cert);
    assert(common_name != nullptr);

    const SslBuffer cert_buffer(&cert);
    const PgBinaryValue cert_der(cert_buffer.get());

    const SslBuffer key_buffer(&key);
    const PgBinaryValue key_der(key_buffer.get());

    const auto alt_names = GetSubjectAltNames(cert);

    const auto not_before = FormatTime(X509_get_notBefore(&cert));
    if (not_before == nullptr)
        throw "Certificate does not have a notBefore time stamp";

    const auto not_after = FormatTime(X509_get_notAfter(&cert));
    if (not_after == nullptr)
        throw "Certificate does not have a notAfter time stamp";

    auto result = CheckError(db.UpdateServerCertificate(common_name.c_str(),
                                                        alt_names,
                                                        not_before.c_str(),
                                                        not_after.c_str(),
                                                        cert_der, key_der));
    if (result.GetAffectedRows() > 0) {
        return false;
    } else {
        CheckError(db.InsertServerCertificate(common_name.c_str(),
                                              alt_names,
                                              not_before.c_str(),
                                              not_after.c_str(),
                                              cert_der, key_der));
        return true;
    }
}

static void
LoadCertificate(const char *cert_path, const char *key_path)
{
    const ScopeSslGlobalInit ssl_init;

    const auto cert = LoadCertFile(cert_path);
    const auto common_name = GetCommonName(cert.get());
    if (common_name == nullptr)
        throw "Certificate has no common name";

    const auto key = LoadKeyFile(key_path);
    if (!MatchModulus(*cert, *key))
        throw "Key and certificate do not match.";

    CertDatabase db(*db_config);

    bool inserted = LoadCertificate(db, *cert, *key);
    printf("%s: %s\n", inserted ? "insert" : "update", common_name.c_str());
    db.NotifyModified();
}

static void
DeleteCertificate(const char *host)
{
    CertDatabase db(*db_config);

    const auto result = CheckError(db.DeleteServerCertificateByName(host));
    if (result.GetAffectedRows() == 0)
        throw "Certificate not found";

    db.NotifyModified();
}

static UniqueX509
FindCertByName(CertDatabase &db, const char *common_name)
{
    auto result = CheckError(db.FindServerCertificateByName(common_name));
    if (result.GetRowCount() == 0)
        return nullptr;

    if (!result.IsColumnBinary(0) || result.IsValueNull(0, 0))
        throw "Unexpected result";

    auto cert_der = result.GetBinaryValue(0, 0);

    auto data = (const unsigned char *)cert_der.data;
    UniqueX509 cert(d2i_X509(nullptr, &data, cert_der.size));
    if (!cert)
        throw "d2i_X509() failed";

    return cert;
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
    auto result = CheckError(db.FindServerCertificateKeyByName(common_name));
    if (result.GetRowCount() == 0)
        return nullptr;

    if (!result.IsColumnBinary(1) || result.IsValueNull(0, 1))
        throw std::runtime_error("Unexpected result");

    auto key_der = result.GetBinaryValue(0, 1);

    auto key_data = (const unsigned char *)key_der.data;
    UniqueEVP_PKEY key(d2i_AutoPrivateKey(nullptr, &key_data, key_der.size));
    if (!key)
        throw SslError("d2i_AutoPrivateKey() failed");

    return key;
}

static UniqueX509
FindCertByHost(const char *host)
{
    CertDatabase db(*db_config);

    auto cert = FindCertByName(db, host);
    if (!cert) {
        auto wildcard = MakeCommonNameWildcard(host);
        if (!wildcard.empty())
            cert = FindCertByName(db, wildcard.c_str());

        if (!cert)
            throw "Certificate not found";
    }

    return cert;
}

static void
FindCertificate(const char *host)
{
    const ScopeSslGlobalInit ssl_init;

    auto cert = FindCertByHost(host);
    X509_print_fp(stdout, cert.get());
    PEM_write_X509(stdout, cert.get());
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
AddExt(X509 *cert, int nid, const char *value)
{
    UniqueX509_EXTENSION ext(X509V3_EXT_conf_nid(nullptr, nullptr, nid,
                                                 const_cast<char *>(value)));
    if (ext == nullptr)
        throw SslError("X509V3_EXT_conf_nid() failed");

    X509_add_ext(cert, ext.get(), -1);
}

static UniqueX509
MakeSelfIssuedDummyCert(const char *common_name)
{
    UniqueX509 cert(X509_new());
    if (cert == nullptr)
        throw "X509_new() failed";

    auto *name = X509_get_subject_name(cert.get());

    if (!X509_NAME_add_entry_by_NID(name, NID_commonName, MBSTRING_ASC,
                                    const_cast<unsigned char *>((const unsigned char *)common_name),
                                    -1, -1, 0))
        throw SslError("X509_NAME_add_entry_by_NID() failed");

    X509_set_issuer_name(cert.get(), name);

    X509_set_version(cert.get(), 2);
    ASN1_INTEGER_set(X509_get_serialNumber(cert.get()), 1);
    X509_gmtime_adj(X509_get_notBefore(cert.get()), 0);
    X509_gmtime_adj(X509_get_notAfter(cert.get()), 60 * 60);

    AddExt(cert.get(), NID_basic_constraints, "critical,CA:TRUE");
    AddExt(cert.get(), NID_key_usage, "critical,keyCertSign");

    return cert;
}

static UniqueX509
MakeSelfSignedDummyCert(EVP_PKEY &key, const char *common_name)
{
    auto cert = MakeSelfIssuedDummyCert(common_name);
    X509_set_pubkey(cert.get(), &key);
    if (!X509_sign(cert.get(), &key, EVP_sha1()))
        throw SslError("X509_sign() failed");

    return cert;
}

static UniqueX509
MakeTlsSni01Cert(EVP_PKEY &account_key, EVP_PKEY &key, const char *host,
                 const AcmeClient::AuthzTlsSni01 &authz)
{
    (void)authz;

    const auto alt_host = authz.MakeDnsName(account_key);
    std::string alt_name = "DNS:" + alt_host;

    auto cert = MakeSelfIssuedDummyCert(host);

    AddExt(cert.get(), NID_subject_alt_name, alt_name.c_str());

    X509_set_pubkey(cert.get(), &key);
    if (!X509_sign(cert.get(), &key, EVP_sha1()))
        throw SslError("X509_sign() failed");

    return cert;
}

static UniqueX509_REQ
MakeCertRequest(EVP_PKEY &key, const char *common_name)
{
    UniqueX509_REQ req(X509_REQ_new());
    if (req == nullptr)
        throw "X509_REQ_new() failed";

    auto *name = X509_REQ_get_subject_name(req.get());

    if (!X509_NAME_add_entry_by_NID(name, NID_commonName, MBSTRING_ASC,
                                    const_cast<unsigned char *>((const unsigned char *)common_name),
                                    -1, -1, 0))
        throw SslError("X509_NAME_add_entry_by_NID() failed");

    X509_REQ_set_pubkey(req.get(), &key);

    if (!X509_REQ_sign(req.get(), &key, EVP_sha1()))
        throw SslError("X509_REQ_sign() failed");

    return req;
}

static void
AcmeNewAuthz(EVP_PKEY &key, CertDatabase &db, AcmeClient &client,
             const char *host)
{
    const auto response = client.NewAuthz(key, host);

    const auto cert_key = GenerateRsaKey();
    const auto cert = MakeTlsSni01Cert(key, *cert_key, host, response);

    LoadCertificate(db, *cert, *cert_key);
    db.NotifyModified();

    printf("Loaded challenge certificate into database\n");

    /* wait until beng-lb's NameCache has been updated; 500ms is
       an arbitrary delay, somewhat bigger than NameCache's 200ms
       delay */
    usleep(500000);

    printf("Waiting for confirmation from ACME server\n");
    bool done = client.UpdateAuthz(key, response);
    while (!done) {
        usleep(100000);
        done = client.CheckAuthz(response);
    }
}

static void
AcmeNewCert(EVP_PKEY &key, CertDatabase &db, AcmeClient &client,
            const char *host)
{
    const auto cert_key = FindKeyByName(db, host);
    if (!cert_key)
        throw "Challenge certificate not found in database";

    const auto req = MakeCertRequest(*cert_key, host);
    const auto cert = client.NewCert(key, *req);

    LoadCertificate(db, *cert, *cert_key);
    db.NotifyModified();
}

static void
Acme(ConstBuffer<const char *> args)
{
    if (args.IsEmpty())
        throw "acme commands:\n"
            "  new-reg EMAIL\n"
            "  new-authz HOST\n"
            "  new-cert HOST\n"
            "  new-authz-cert HOST\n"
            "\n"
            "options:\n"
            "  --staging     use the Let's Encrypt staging server\n";

    const char *key_path = "/etc/cm4all/acme/account.key";

    bool staging = false;
    if (!args.IsEmpty() && strcmp(args.front(), "--staging") == 0) {
        args.shift();
        staging = true;
    }

    const auto cmd = args.shift();

    if (strcmp(cmd, "new-reg") == 0) {
        if (args.size != 1)
            throw "Usage: acme new-reg EMAIL";

        const char *email = args[0];

        const ScopeSslGlobalInit ssl_init;

        const auto key = LoadKeyFile(key_path);
        if (EVP_PKEY_type(key->type) != EVP_PKEY_RSA)
            throw "RSA key expected";

        const auto account = AcmeClient(staging).NewReg(*key, email);
        printf("location: %s\n", account.location.c_str());
    } else if (strcmp(cmd, "new-authz") == 0) {
        if (args.size != 1)
            throw "Usage: acme new-authz HOST";

        const char *host = args[0];

        const ScopeSslGlobalInit ssl_init;

        const auto key = LoadKeyFile(key_path);
        if (EVP_PKEY_type(key->type) != EVP_PKEY_RSA)
            throw "RSA key expected";

        CertDatabase db(*db_config);
        AcmeClient client(staging);

        AcmeNewAuthz(*key, db, client, host);
        printf("OK\n");
    } else if (strcmp(cmd, "new-cert") == 0) {
        if (args.size != 1)
            throw "Usage: acme new-cert HOST";

        const char *host = args[0];

        const ScopeSslGlobalInit ssl_init;

        const auto key = LoadKeyFile(key_path);
        if (EVP_PKEY_type(key->type) != EVP_PKEY_RSA)
            throw "RSA key expected";

        CertDatabase db(*db_config);
        AcmeClient client(staging);

        AcmeNewCert(*key, db, client, host);
        printf("OK\n");
    } else if (strcmp(cmd, "new-authz-cert") == 0) {
        if (args.size != 1)
            throw "Usage: acme new-authz-cert HOST";

        const char *host = args[0];

        const ScopeSslGlobalInit ssl_init;

        const auto key = LoadKeyFile(key_path);
        if (EVP_PKEY_type(key->type) != EVP_PKEY_RSA)
            throw "RSA key expected";

        CertDatabase db(*db_config);
        AcmeClient client(staging);

        AcmeNewAuthz(*key, db, client, host);
        AcmeNewCert(*key, db, client, host);
        printf("OK\n");
    } else
        throw "Unknown acme command";
}

static void
Populate(CertDatabase &db, EVP_PKEY *key, PgBinaryValue key_der,
         const char *common_name)
{
    (void)key;

    // TODO: fake time stamps
    const char *not_before = "1971-01-01";
    const char *not_after = "1971-01-01";

    auto cert = MakeSelfSignedDummyCert(*key, common_name);
    const SslBuffer cert_buffer(cert.get());
    const PgBinaryValue cert_der(cert_buffer.get());

    const auto alt_names = GetSubjectAltNames(*cert);

    CheckError(db.InsertServerCertificate(common_name, alt_names,
                                          not_before, not_after,
                                          cert_der, key_der));
}

static void
Populate(const char *key_path, const char *suffix, unsigned n)
{
    const ScopeSslGlobalInit ssl_init;

    const auto key = LoadKeyFile(key_path);

    const SslBuffer key_buffer(key.get());
    const PgBinaryValue key_der(key_buffer.get());

    CertDatabase db(*db_config);

    if (n == 0) {
        Populate(db, key.get(), key_der, suffix);
    } else {
        if (!db.BeginSerializable())
            throw "BEGIN failed";

        for (unsigned i = 1; i <= n; ++i) {
            char buffer[256];
            snprintf(buffer, sizeof(buffer), "%u%s", i, suffix);
            Populate(db, key.get(), key_der, buffer);
        }

        if (!db.Commit())
            throw "COMMIT failed";
    }

    db.NotifyModified();
}

int
main(int argc, char **argv)
{
    ConstBuffer<const char *> args(argv + 1, argc - 1);

    if (args.IsEmpty()) {
        fprintf(stderr, "Usage: %s COMMAND ...\n"
                "\n"
                "Commands:\n"
                "  load CERT KEY\n"
                "  delete HOST\n"
                "  find HOST\n"
                "  monitor\n"
                "  tail\n"
                "  acme ...\n"
                "  populate KEY COUNT SUFFIX\n"
                "\n", argv[0]);
        return EXIT_FAILURE;
    }

    const auto cmd = args.shift();

    try {
        LbConfig lb_config = lb_config_load(RootPool(),
                                            "/etc/cm4all/beng/lb.conf");
        {
            auto i = lb_config.cert_dbs.begin();
            if (i == lb_config.cert_dbs.end())
                throw "/etc/cm4all/beng/lb.conf does contains no cert_db section";

            if (std::next(i) != lb_config.cert_dbs.end())
                fprintf(stderr, "Warning: /etc/cm4all/beng/lb.conf does contains multiple cert_db sections\n");

            db_config = &i->second;
        }

        if (strcmp(cmd, "load") == 0) {
            if (args.size != 2) {
                fprintf(stderr, "Usage: %s load CERT KEY\n", argv[0]);
                return EXIT_FAILURE;
            }

            LoadCertificate(args[0], args[1]);
        } else if (strcmp(cmd, "delete") == 0) {
            if (args.size != 1) {
                fprintf(stderr, "Usage: %s delete HOST\n", argv[0]);
                return EXIT_FAILURE;
            }

            DeleteCertificate(args[0]);
        } else if (strcmp(cmd, "find") == 0) {
            if (args.size != 1) {
                fprintf(stderr, "Usage: %s find HOST\n", argv[0]);
                return EXIT_FAILURE;
            }

            FindCertificate(args[0]);
        } else if (strcmp(cmd, "monitor") == 0) {
            if (args.size != 0) {
                fprintf(stderr, "Usage: %s monitor\n", argv[0]);
                return EXIT_FAILURE;
            }

            Monitor();
        } else if (strcmp(cmd, "tail") == 0) {
            if (args.size != 0) {
                fprintf(stderr, "Usage: %s tail\n", argv[0]);
                return EXIT_FAILURE;
            }

            Tail();
        } else if (strcmp(cmd, "acme") == 0) {
            Acme(args);
        } else if (strcmp(cmd, "populate") == 0) {
            if (args.size < 2 || args.size > 3) {
                fprintf(stderr, "Usage: %s populate KEY {HOST|SUFFIX COUNT}\n",
                        argv[0]);
                return EXIT_FAILURE;
            }

            const char *key = args[0];
            const char *suffix = args[1];
            unsigned count = 0;

            if (args.size == 3) {
                count = strtoul(args[2], nullptr, 10);
                if (count == 0) {
                    fprintf(stderr, "Invalid COUNT parameter\n");
                    return EXIT_FAILURE;
                }
            }

            Populate(key, suffix, count);
        } else {
            fprintf(stderr, "Unknown command: %s\n", cmd);
            return EXIT_FAILURE;
        }

        return EXIT_SUCCESS;
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    } catch (const char *msg) {
        fprintf(stderr, "%s\n", msg);
        return EXIT_FAILURE;
    }
}
