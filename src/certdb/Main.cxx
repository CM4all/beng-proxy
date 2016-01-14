#include "Config.hxx"
#include "CertDatabase.hxx"
#include "Wildcard.hxx"
#include "ssl/ssl_init.hxx"
#include "ssl/Util.hxx"
#include "ssl/Name.hxx"
#include "ssl/MemBio.hxx"
#include "ssl/Unique.hxx"
#include "ssl/Error.hxx"
#include "pg/Error.hxx"
#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

#include <openssl/ts.h>

#include <stdexcept>

#include <stdio.h>
#include <stdlib.h>
#include <poll.h>

static const CertDatabaseConfig config{"dbname=lb", std::string()};

class SslBuffer {
    unsigned char *data = nullptr;
    size_t size;

public:
    explicit SslBuffer(X509 *cert) {
        int result = i2d_X509(cert, &data);
        if (result < 0)
            throw SslError("Failed to encode certificate");

        size = result;
    }

    explicit SslBuffer(EVP_PKEY *key) {
        int result = i2d_PrivateKey(key, &data);
        if (result < 0)
            throw SslError("Failed to encode key");

        size = result;
    }

    SslBuffer(SslBuffer &&src):data(src.data), size(src.size) {
        src.data = nullptr;
    }

    ~SslBuffer() {
        if (data != nullptr)
            OPENSSL_free(data);
    }

    PgBinaryValue ToPg() const {
        return {data, size};
    }
};

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

    CertDatabase db(config);

    const SslBuffer cert_buffer(cert.get());
    const auto cert_der = cert_buffer.ToPg();

    const SslBuffer key_buffer(key.get());
    const auto key_der = key_buffer.ToPg();

    const auto not_before = FormatTime(X509_get_notBefore(cert));
    if (not_before == nullptr)
        throw "Certificate does not have a notBefore time stamp";

    const auto not_after = FormatTime(X509_get_notAfter(cert));
    if (not_after == nullptr)
        throw "Certificate does not have a notAfter time stamp";

    auto result = CheckError(db.UpdateServerCertificate(common_name.c_str(),
                                                        not_before.c_str(),
                                                        not_after.c_str(),
                                                        cert_der, key_der));
    if (result.GetAffectedRows() > 0) {
        printf("update: %s\n", common_name.c_str());
    } else {
        CheckError(db.InsertServerCertificate(common_name.c_str(),
                                              not_before.c_str(),
                                              not_after.c_str(),
                                              cert_der, key_der));
        printf("insert: %s\n", common_name.c_str());
    }

    db.NotifyModified();
}

static void
DeleteCertificate(const char *host)
{
    CertDatabase db(config);

    const auto result = CheckError(db.DeleteServerCertificateByCommonName(host));
    if (result.GetAffectedRows() == 0)
        throw "Certificate not found";

    db.NotifyModified();
}

static UniqueX509
FindCertByCommonName(CertDatabase &db, const char *common_name)
{
    auto result = CheckError(db.FindServerCertificateByCommonName(common_name));
    if (result.GetRowCount() == 0)
        return nullptr;

    if (!result.IsColumnBinary(0) || result.IsValueNull(0, 0))
        throw "Unexpected result";

    auto cert_der = result.GetBinaryValue(0, 0);

    auto data = (const unsigned char *)cert_der.value;
    UniqueX509 cert(d2i_X509(nullptr, &data, cert_der.size));
    if (!cert)
        throw "d2i_X509() failed";

    return cert;
}

static UniqueX509
FindCertByHost(const char *host)
{
    CertDatabase db(config);

    auto cert = FindCertByCommonName(db, host);
    if (!cert) {
        auto wildcard = MakeCommonNameWildcard(host);
        if (!wildcard.empty())
            cert = FindCertByCommonName(db, wildcard.c_str());

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
    CertDatabase db(config);
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
    CertDatabase db(config);

    for (auto &row : CheckError(db.TailModifiedServerCertificatesMeta()))
        printf("%s %s %s\n",
               row.GetValue(1),
               *row.GetValue(0) == 't' ? "deleted" : "modified",
               row.GetValue(2));
}

static UniqueX509_NAME
MakeName(const char *common_name)
{
    UniqueX509_NAME n(X509_NAME_new());
    if (n == nullptr)
        throw "X509_NAME_new() failed";

    X509_NAME_add_entry_by_NID(n.get(), NID_commonName, MBSTRING_ASC,
                               const_cast<unsigned char *>((const unsigned char *)common_name),
                               -1, -1, 0);
    return n;
}

static UniqueX509
MakeSelfSignedDummyCert(const char *common_name)
{
    const auto n = MakeName(common_name);
    UniqueX509 cert(X509_new());
    if (cert == nullptr)
        throw "X509_new() failed";

    X509_set_subject_name(cert.get(), n.get());
    return cert;
}

static void
Populate(CertDatabase &db, EVP_PKEY *key, PgBinaryValue key_der,
         const char *common_name)
{
    // TODO: sign certificate
    (void)key;

    // TODO: fake time stamps
    const char *not_before = "1971-01-01";
    const char *not_after = "1971-01-01";

    auto cert = MakeSelfSignedDummyCert(common_name);
    const SslBuffer cert_buffer(cert.get());
    const auto cert_der = cert_buffer.ToPg();

    CheckError(db.InsertServerCertificate(common_name, not_before, not_after,
                                          cert_der, key_der));
}

static void
Populate(const char *key_path, const char *suffix, unsigned n)
{
    const ScopeSslGlobalInit ssl_init;

    const auto key = LoadKeyFile(key_path);

    const SslBuffer key_buffer(key.get());
    const auto key_der = key_buffer.ToPg();

    CertDatabase db(config);

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
                "  populate KEY COUNT SUFFIX\n"
                "\n", argv[0]);
        return EXIT_FAILURE;
    }

    const auto cmd = args.shift();

    try {
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
