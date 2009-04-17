/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TRANSLATE_H
#define __BENG_TRANSLATE_H

#include "pool.h"
#include "http.h"
#include "resource-address.h"

#include <stdint.h>

struct uri_with_address;
struct hstock;
struct async_operation_ref;

struct translate_request {
    const char *remote_host;
    const char *host;
    const char *user_agent;
    const char *accept_language;
    const char *uri;
    const char *query_string;
    const char *widget_type;
    const char *session;
    const char *param;
};

struct translate_response {
    unsigned max_age;

    http_status_t status;

    struct resource_address address;

    const char *site;
    const char *document_root;
    const char *redirect;

    const char *host;
    bool stateful;

    const char *session;

    const char *user;
    unsigned user_max_age;

    const char *language;

    struct transformation_view *views;

    const uint16_t *vary;
    unsigned num_vary;
};

typedef void (*translate_callback_t)(const struct translate_response *response,
                                     void *ctx);

void
translate(pool_t pool,
          struct hstock *tcp_stock, const char *socket_path,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx,
          struct async_operation_ref *async_ref);

#endif
