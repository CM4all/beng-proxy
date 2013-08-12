/*
 * SSL/TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_FILTER_H
#define BENG_PROXY_SSL_FILTER_H

#include "ssl_quark.h"

#include <inline/compiler.h>

struct pool;
struct ssl_factory;
struct notify;
struct ssl_config;
struct ssl_filter;

/**
 * Create a new SSL filter.  It is run in a new thread.
 *
 * @param encrypted_fd the encrypted side of the filter
 * @param plain_fd the plain-text side of the filter (socketpair
 * to local service)
 */
struct ssl_filter *
ssl_filter_new(struct pool *pool, struct ssl_factory *factory,
               int encrypted_fd, int plain_fd,
               struct notify *notify,
               GError **error_r);

void
ssl_filter_free(struct ssl_filter *ssl);

gcc_pure
const char *
ssl_filter_get_peer_subject(struct ssl_filter *ssl);

gcc_pure
const char *
ssl_filter_get_peer_issuer_subject(struct ssl_filter *ssl);

#endif
