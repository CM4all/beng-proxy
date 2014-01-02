#ifndef BENG_PROXY_FCGI_QUARK_H
#define BENG_PROXY_FCGI_QUARK_H

#include <glib.h>

static inline GQuark
fcgi_quark(void)
{
    return g_quark_from_static_string("fastcgi");
}

#endif
