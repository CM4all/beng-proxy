#include "Config.hxx"
#include "CertDatabase.hxx"
#include "Wildcard.hxx"
#include "ssl/Util.hxx"
#include "ssl/Name.hxx"
#include "ssl/MemBio.hxx"
#include "ssl/Unique.hxx"
#include "pg/Error.hxx"
#include "util/ConstBuffer.hxx"

#include <inline/compiler.h>

#include <openssl/ts.h>

#include <stdexcept>

#include <stdio.h>
#include <stdlib.h>
#include <poll.h>

static const CertDatabaseConfig config{"dbname=lb", std::string()};

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

static int
LoadCertificate(const char *cert_path, const char *key_path)
{
    X509 *cert = TS_CONF_load_cert(cert_path);
    if (cert == nullptr) {
        fprintf(stderr, "Failed to load certificate\n");
        return EXIT_FAILURE;
    }

    const auto common_name = GetCommonName(cert);
    if (common_name == nullptr) {
        fprintf(stderr, "Certificate has no common name\n");
        return EXIT_FAILURE;
    }

    auto key = TS_CONF_load_key(key_path, nullptr);
    if (key == nullptr) {
        fprintf(stderr, "Failed to load key\n");
        return EXIT_FAILURE;
    }

    if (!MatchModulus(*cert, *key)) {
        fprintf(stderr, "Key and certificate do not match.\n");
        return EXIT_FAILURE;
    }

    CertDatabase db(config);

    unsigned char *cert_der_buffer = nullptr;
    int cert_der_length = i2d_X509(cert, &cert_der_buffer);
    if (cert_der_length < 0) {
        fprintf(stderr, "Failed to encode certificate\n");
        return EXIT_FAILURE;
    }

    const PgBinaryValue cert_der(cert_der_buffer, cert_der_length);

    unsigned char *key_der_buffer = nullptr;
    int key_der_length = i2d_PrivateKey(key, &key_der_buffer);
    if (key_der_length < 0) {
        fprintf(stderr, "Failed to encode key\n");
        return EXIT_FAILURE;
    }

    const PgBinaryValue key_der(key_der_buffer, key_der_length);

    const auto not_before = FormatTime(X509_get_notBefore(cert));
    if (not_before == nullptr) {
        fprintf(stderr, "Certificate does not have a notBefore time stamp\n");
        return EXIT_FAILURE;
    }

    const auto not_after = FormatTime(X509_get_notAfter(cert));
    if (not_after == nullptr) {
        fprintf(stderr, "Certificate does not have a notAfter time stamp\n");
        return EXIT_FAILURE;
    }

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

    return EXIT_SUCCESS;
}

static int
DeleteCertificate(const char *host)
{
    CertDatabase db(config);

    const auto result = CheckError(db.DeleteServerCertificateByCommonName(host));
    if (result.GetAffectedRows() == 0)
        throw "Certificate not found";

    return EXIT_SUCCESS;
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

static int
FindCertificate(const char *host)
{
    auto cert = FindCertByHost(host);
    X509_print_fp(stdout, cert.get());
    PEM_write_X509(stdout, cert.get());
    return EXIT_SUCCESS;
}

static int
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

    return EXIT_SUCCESS;
}

static int
Tail()
{
    CertDatabase db(config);

    for (auto &row : CheckError(db.TailModifiedServerCertificatesMeta()))
        printf("%s %s %s\n",
               row.GetValue(1),
               *row.GetValue(0) == 't' ? "deleted" : "modified",
               row.GetValue(2));

    return EXIT_SUCCESS;
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

            return LoadCertificate(args[0], args[1]);
        } else if (strcmp(cmd, "delete") == 0) {
            if (args.size != 1) {
                fprintf(stderr, "Usage: %s delete HOST\n", argv[0]);
                return EXIT_FAILURE;
            }

            return DeleteCertificate(args[0]);
        } else if (strcmp(cmd, "find") == 0) {
            if (args.size != 1) {
                fprintf(stderr, "Usage: %s find HOST\n", argv[0]);
                return EXIT_FAILURE;
            }

            return FindCertificate(args[0]);
        } else if (strcmp(cmd, "monitor") == 0) {
            if (args.size != 0) {
                fprintf(stderr, "Usage: %s monitor\n", argv[0]);
                return EXIT_FAILURE;
            }

            return Monitor();
        } else if (strcmp(cmd, "tail") == 0) {
            if (args.size != 0) {
                fprintf(stderr, "Usage: %s tail\n", argv[0]);
                return EXIT_FAILURE;
            }

            return Tail();
        } else {
            fprintf(stderr, "Unknown command: %s\n", cmd);
            return EXIT_FAILURE;
        }
    } catch (const std::exception &e) {
        fprintf(stderr, "%s\n", e.what());
        return EXIT_FAILURE;
    } catch (const char *msg) {
        fprintf(stderr, "%s\n", msg);
        return EXIT_FAILURE;
    }
}
