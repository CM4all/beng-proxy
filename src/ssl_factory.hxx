/*
 * SSL/TLS initialisation.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_FACTORY_H
#define BENG_PROXY_SSL_FACTORY_H

struct pool;
struct ssl_config;
class Error;

struct ssl_factory *
ssl_factory_new(const ssl_config &config,
                bool server,
                Error &error);

void
ssl_factory_free(struct ssl_factory *factory);

struct ssl_st *
ssl_factory_make(struct ssl_factory &factory);

#endif
