/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_QUARK_HXX
#define BENG_PROXY_NFS_QUARK_HXX

#include <glib.h>

G_GNUC_CONST
static inline GQuark
nfs_client_quark(void)
{
    return g_quark_from_static_string("nfs_client");
}

#endif
