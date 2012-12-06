/*
 * SSL/TLS configuration.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CONFIG_H
#define BENG_PROXY_SSL_CONFIG_H

#include <stdbool.h>

struct ssl_config {
    const char *cert_file;

    const char *key_file;

    const char *ca_cert_file;

    bool verify;
};

static inline void
ssl_config_clear(struct ssl_config *config)
{
    config->cert_file = config->key_file = NULL;
    config->ca_cert_file = NULL;
    config->verify = false;
}

static inline bool
ssl_config_valid(const struct ssl_config *config)
{
    return config->cert_file != NULL && config->key_file != NULL;
}

#endif
