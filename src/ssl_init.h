/*
 * OpenSSL global initialization.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_INIT_H
#define BENG_PROXY_SSL_INIT_H

void
ssl_global_init(void);

void
ssl_global_deinit(void);

#endif
