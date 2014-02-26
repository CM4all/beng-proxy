/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_CLIENT_H
#define BENG_PROXY_TRANSLATE_CLIENT_H

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
struct translate_request;
struct translate_response;

struct translate_handler {
    void (*response)(const struct translate_response *response, void *ctx);
    void (*error)(GError *error, void *ctx);
};

G_GNUC_CONST
static inline GQuark
translate_quark(void)
{
    return g_quark_from_static_string("translate");
}

#ifdef __cplusplus
extern "C" {
#endif

void
translate(struct pool *pool, int fd,
          const struct lease *lease, void *lease_ctx,
          const struct translate_request *request,
          const struct translate_handler *handler, void *ctx,
          struct async_operation_ref *async_ref);

#ifdef __cplusplus
}
#endif

#endif
