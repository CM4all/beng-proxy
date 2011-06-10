/*
 * SSL/TLS initialisation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CREATE_H
#define BENG_PROXY_SSL_CREATE_H

#include "ssl_quark.h"

#include <openssl/ssl.h>

struct ssl_config;

SSL_CTX *
ssl_create(const struct ssl_config *config, GError **error_r);

#endif
