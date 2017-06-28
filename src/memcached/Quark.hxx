/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef MEMCACHED_QUARK_HXX
#define MEMCACHED_QUARK_HXX

#include <glib.h>

G_GNUC_CONST
static inline GQuark
memcached_client_quark(void)
{
    return g_quark_from_static_string("memcached_client");
}

#endif
