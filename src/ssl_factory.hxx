/*
 * SSL/TLS initialisation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_FACTORY_H
#define BENG_PROXY_SSL_FACTORY_H

#include "gerror.h"

struct pool;
struct ssl_config;

struct ssl_factory *
ssl_factory_new(struct pool *pool, const struct ssl_config *config,
                GError **error_r);

void
ssl_factory_free(struct ssl_factory *factory);

struct ssl_st *
ssl_factory_make(struct ssl_factory *factory);

#endif
