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
#include "header-forward.h"

#include <stdint.h>

struct sockaddr;
struct lease;
struct async_operation_ref;

struct translate_request {
    const struct sockaddr *local_address;
    size_t local_address_length;

    const char *remote_host;
    const char *host;
    const char *user_agent;
    const char *accept_language;
    const char *uri;
    const char *args;
    const char *query_string;
    const char *widget_type;
    const char *session;
    const char *param;
};

struct translate_response {
    unsigned max_age;

    http_status_t status;

    struct resource_address address;

    /**
     * Which request headers are forwarded?
     */
    struct header_forward_settings request_header_forward;

    /**
     * Which response headers are forwarded?
     */
    struct header_forward_settings response_header_forward;

    const char *base;

    const char *site;
    const char *document_root;
    const char *redirect;
    const char *bounce;

    const char *scheme;
    const char *host;
    const char *uri;

    const char *untrusted;

    bool stateful;

    bool discard_session;

    const char *session;

    const char *user;
    unsigned user_max_age;

    const char *language;

    struct transformation_view *views;

    const uint16_t *vary;
    unsigned num_vary;

    const uint16_t *invalidate;
    unsigned num_invalidate;
};

typedef void (*translate_callback_t)(const struct translate_response *response,
                                     void *ctx);

void
translate(pool_t pool, int fd,
          const struct lease *lease, void *lease_ctx,
          const struct translate_request *request,
          translate_callback_t callback,
          void *ctx,
          struct async_operation_ref *async_ref);

#endif
