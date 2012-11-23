/*
 * SSL/TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_FILTER_H
#define BENG_PROXY_SSL_FILTER_H

#include "gerror.h"

#include <inline/compiler.h>

struct pool;
struct ssl_factory;
struct notify;
struct ssl_config;
struct ssl_filter;

/**
 * A module for #thread_socket_filter that encrypts all data with
 * SSL/TLS.  Call ssl_filter_new() to create an instance.
 */
extern const struct ThreadSocketFilterHandler ssl_thread_socket_filter;

/**
 * Create a new SSL filter, to be used with #ssl_thread_socket_filter.
 *
 * @param encrypted_fd the encrypted side of the filter
 * @param plain_fd the plain-text side of the filter (socketpair
 * to local service)
 */
struct ssl_filter *
ssl_filter_new(struct pool *pool, struct ssl_factory *factory,
               GError **error_r);

gcc_pure
const char *
ssl_filter_get_peer_subject(struct ssl_filter *ssl);

gcc_pure
const char *
ssl_filter_get_peer_issuer_subject(struct ssl_filter *ssl);

#endif
