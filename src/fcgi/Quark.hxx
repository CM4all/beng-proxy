/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_FCGI_QUARK_HXX
#define BENG_PROXY_FCGI_QUARK_HXX

#include <glib.h>

static inline GQuark
fcgi_quark()
{
    return g_quark_from_static_string("fastcgi");
}

#endif
