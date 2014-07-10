/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_UA_CLASSIFICATION_H
#define BENG_PROXY_UA_CLASSIFICATION_H

#include <inline/compiler.h>

#include <glib.h>
#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

G_GNUC_CONST
static inline GQuark
ua_classification_quark(void)
{
    return g_quark_from_static_string("ua_classification");
}

bool
ua_classification_init(const char *path, GError **error_r);

void
ua_classification_deinit(void);

gcc_pure
const char *
ua_classification_lookup(const char *user_agent);

#ifdef __cplusplus
}
#endif

#endif
