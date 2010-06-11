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

    /**
     * The value of the "Authorization" HTTP request header.
     */
    const char *authorization;

    const char *uri;
    const char *args;
    const char *query_string;
    const char *widget_type;
    const char *session;
    const char *param;

    http_status_t error_document_status;
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
    const char *untrusted_prefix;

    bool stateful;

    bool discard_session;

    bool secure_cookie;

    bool filter_4xx;

    bool error_document;

    const char *session;

    const char *user;
    unsigned user_max_age;

    const char *language;

    /**
     * The value of the "WWW-Authenticate" HTTP response header.
     */
    const char *www_authenticate;

    /**
     * The value of the "Authentication-Info" HTTP response header.
     */
    const char *authentication_info;

    struct strmap *headers;

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
