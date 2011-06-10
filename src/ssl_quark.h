/*
 * SSL/TLS GQuark.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_SSL_QUARK_H
#define BENG_PROXY_SSL_QUARK_H

#include <glib.h>

G_GNUC_CONST
static inline GQuark
ssl_quark(void)
{
    return g_quark_from_static_string("ssl");
}

#endif
