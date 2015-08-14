/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_QUARK_HXX
#define BENG_PROXY_TRANSLATE_QUARK_HXX

#include <glib.h>

G_GNUC_CONST
static inline GQuark
translate_quark(void)
{
    return g_quark_from_static_string("translate");
}

#endif
