/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_HTTP_QUARK_H
#define BENG_PROXY_HTTP_QUARK_H

#include <glib.h>

/**
 * A GQuark for GError that gets translated into a HTTP response.  The
 * "code" is the HTTP status code.
 */
G_GNUC_CONST
static inline GQuark
http_response_quark(void)
{
    return g_quark_from_static_string("http_response");
}

#endif
