/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_CLIENT_HXX
#define BENG_PROXY_TRANSLATE_CLIENT_HXX

#include "resource-address.h"
#include "header-forward.h"
#include "strref.h"

#include <http/status.h>

#include <glib.h>

#include <stdint.h>

struct pool;
struct sockaddr;
struct lease;
struct async_operation_ref;
struct TranslateRequest;
struct TranslateResponse;

struct TranslateHandler {
    void (*response)(const TranslateResponse *response, void *ctx);
    void (*error)(GError *error, void *ctx);
};

G_GNUC_CONST
static inline GQuark
translate_quark(void)
{
    return g_quark_from_static_string("translate");
}

void
translate(struct pool *pool, int fd,
          const struct lease *lease, void *lease_ctx,
          const TranslateRequest *request,
          const TranslateHandler *handler, void *ctx,
          struct async_operation_ref *async_ref);

#endif
