/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ACME_CLIENT_HXX
#define BENG_PROXY_ACME_CLIENT_HXX

#include "event/Base.hxx"
#include "RootPool.hxx"
#include "GlueHttpClient.hxx"
#include "ssl/Unique.hxx"
#include "util/ConstBuffer.hxx"

#include <string>

/**
 * Implementation of a ACME client, i.e. the protocol of the "Let's
 * Encrypt" project.
 *
 * @see https://ietf-wg-acme.github.io/acme/
 */
class AcmeClient {
    EventBase event_base;
    RootPool root_pool;
    GlueHttpClient glue_http_client;
    GlueHttpServerAddress server;

    /**
     * A replay nonce that was received in the previous request.  It
     * is remembered for the next NextNonce() call, to save a HTTP
     * request.
     */
    std::string next_nonce;

public:
    explicit AcmeClient(bool staging);
    ~AcmeClient();

    struct pool &GetRootPool() {
        return root_pool;
    }

    struct Account {
        std::string location;
    };

    /**
     * Register a new account.
     *
     * @param key the account key
     * @param email an email address to be associated with the account
     */
    Account NewReg(EVP_PKEY &key, const char *email);

    struct AuthzTlsSni01 {
        std::string token;
        std::string uri;

        /**
         * Generate a DNS name for the temporary certificate, to be
         * used as subjectAltName.
         */
        std::string MakeDnsName(EVP_PKEY &key) const;
    };

    /**
     * Create a new "authz" object, to prepare for a new certificate.
     *
     * After this method succeeds, configure the web server with a new
     * temporary certificate using AuthzTlsSni01::MakeDnsName(), and
     * then call UpdateAuthz().
     *
     * @param key the account key
     * @param host the host name ("common name") for the new certificate
     */
    AuthzTlsSni01 NewAuthz(EVP_PKEY &key, const char *host);

    /**
     * Update the "authz" object.  Call this method after NewAuthz().
     *
     * If this method returns false, call CheckAuthz() repeatedly with
     * a reasonable delay.
     *
     * @param key the account key
     * @param authz the return value of NewAuthz()
     * @return true if the authz object is done, and NewCert() can be
     * called
     */
    bool UpdateAuthz(EVP_PKEY &key, const AuthzTlsSni01 &authz);

    /**
     * Check whether the "authz" object is done.  Call this method
     * repeatedly after UpdateAuthz() with a reasonable delay.
     *
     * @param key the account key
     * @param authz the return value of NewAuthz()
     * @return true if the authz object is done, and NewCert() can be
     * called
     */
    bool CheckAuthz(const AuthzTlsSni01 &authz);

    /**
     * Ask the server to produce a signed certificate.
     *
     * @param key the account key
     * @param req the certificate request signed with the certificate
     * key (not with the account key!)
     */
    UniqueX509 NewCert(EVP_PKEY &key, X509_REQ &req);

private:
    /**
     * Ask the server for a new replay nonce.
     */
    std::string RequestNonce();

    /**
     * Obtain a replay nonce.
     */
    std::string NextNonce();

    GlueHttpResponse Request(struct pool &p,
                             http_method_t method, const char *uri,
                             HttpHeaders &&headers=HttpHeaders(),
                             Istream *body=nullptr);

    GlueHttpResponse SignedRequest(struct pool &p, EVP_PKEY &key,
                                   http_method_t method, const char *uri,
                                   HttpHeaders &&headers,
                                   ConstBuffer<void> payload);

    GlueHttpResponse SignedRequest(struct pool &p, EVP_PKEY &key,
                                   http_method_t method, const char *uri,
                                   HttpHeaders &&headers,
                                   const char *payload) {
        return SignedRequest(p, key, method, uri, std::move(headers),
                             ConstBuffer<void>(payload, strlen(payload)));
    }
};

#endif
