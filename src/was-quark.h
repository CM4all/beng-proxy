/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_WAS_QUARK_H
#define BENG_PROXY_WAS_QUARK_H

#include <glib.h>

static inline GQuark
was_quark(void)
{
    return g_quark_from_static_string("was");
}

#endif
