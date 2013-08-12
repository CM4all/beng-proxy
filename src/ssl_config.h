/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CONFIG_H
#define BENG_PROXY_SSL_CONFIG_H

#include <stdbool.h>

enum ssl_verify {
    SSL_VERIFY_NO,
    SSL_VERIFY_YES,
    SSL_VERIFY_OPTIONAL,
};

struct ssl_cert_key_config {
    struct ssl_cert_key_config *next;

    const char *cert_file;

    const char *key_file;
};

struct ssl_config {
    struct ssl_cert_key_config cert_key;

    const char *ca_cert_file;

    enum ssl_verify verify;
};

static inline void
ssl_config_clear(struct ssl_config *config)
{
    config->cert_key.next = NULL;
    config->cert_key.cert_file = config->cert_key.key_file = NULL;
    config->ca_cert_file = NULL;
    config->verify = SSL_VERIFY_NO;
}

static inline bool
ssl_config_valid(const struct ssl_config *config)
{
    return config->cert_key.cert_file != NULL &&
        config->cert_key.key_file != NULL;
}

#endif
