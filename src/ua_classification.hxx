/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UA_CLASSIFICATION_HXX
#define BENG_PROXY_UA_CLASSIFICATION_HXX

#include <inline/compiler.h>

#include <glib.h>

G_GNUC_CONST
static inline GQuark
ua_classification_quark()
{
    return g_quark_from_static_string("ua_classification");
}

bool
ua_classification_init(const char *path, GError **error_r);

void
ua_classification_deinit();

gcc_pure
const char *
ua_classification_lookup(const char *user_agent);

#endif
