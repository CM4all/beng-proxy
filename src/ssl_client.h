/*
 * Glue code for using the ssl_filter in a client connection.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_CLIENT_H
#define BENG_PROXY_SSL_CLIENT_H

#include <glib.h>

struct pool;

#ifdef __cplusplus
extern "C" {
#endif

void
ssl_client_init(void);

void
ssl_client_deinit(void);

const struct socket_filter *
ssl_client_get_filter(void);

void *
ssl_client_create(struct pool *pool,
                  const char *hostname,
                  GError **error_r);

#ifdef __cplusplus
}
#endif

#endif
