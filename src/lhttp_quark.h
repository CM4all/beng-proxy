#ifndef BENG_PROXY_LHTTP_QUARK_H
#define BENG_PROXY_LHTTP_QUARK_H

#include <glib.h>

G_GNUC_CONST
static inline GQuark
lhttp_quark(void)
{
    return g_quark_from_static_string("lhttp");
}

#endif
