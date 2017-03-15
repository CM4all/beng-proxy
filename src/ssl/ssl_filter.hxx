/*
 * SSL/TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_FILTER_H
#define BENG_PROXY_SSL_FILTER_H

#include "Unique.hxx"

#include <inline/compiler.h>

struct SslFactory;
struct SslFilter;
class ThreadSocketFilterHandler;

/**
 * Create a new SSL filter.
 */
SslFilter *
ssl_filter_new(UniqueSSL &&ssl);

/**
 * Create a new SSL filter.
 *
 * Throws std::runtime_error on error.
 *
 * @param encrypted_fd the encrypted side of the filter
 * @param plain_fd the plain-text side of the filter (socketpair
 * to local service)
 */
SslFilter *
ssl_filter_new(SslFactory &factory);

ThreadSocketFilterHandler &
ssl_filter_get_handler(SslFilter &ssl);

gcc_pure
const char *
ssl_filter_get_peer_subject(SslFilter *ssl);

gcc_pure
const char *
ssl_filter_get_peer_issuer_subject(SslFilter *ssl);

#endif
