/*
 * SSL and TLS filter.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "ssl_init.h"

#include <openssl/ssl.h>

void
ssl_global_init(void)
{
    SSL_load_error_strings();
    SSL_library_init();
}
