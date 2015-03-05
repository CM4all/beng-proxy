/*
 * Glue code for using the ssl_filter in a client connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CLIENT_HXX
#define BENG_PROXY_SSL_CLIENT_HXX

#include "glibfwd.hxx"

struct pool;
struct SocketFilter;

void
ssl_client_init();

void
ssl_client_deinit();

const SocketFilter &
ssl_client_get_filter();

void *
ssl_client_create(struct pool *pool,
                  const char *hostname,
                  GError **error_r);

#endif
