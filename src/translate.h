/*
 * Call the translation server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef __BENG_TRANSLATE_H
#define __BENG_TRANSLATE_H

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

    /**
     * The payload of the CHECK packet.  If
     * strref_is_null(&esponse.check), then no CHECK packet will be
     * sent.
     */
    struct strref check;

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

    const char *regex;
    const char *inverse_regex;

    const char *site;
    const char *document_root;
    const char *redirect;
    const char *bounce;

    const char *scheme;
    const char *host;
    const char *uri;

    const char *untrusted;
    const char *untrusted_prefix;
    const char *untrusted_site_suffix;

    bool stateful;

    bool discard_session;

    bool secure_cookie;

    bool filter_4xx;

    bool error_document;

    bool previous;

    bool transparent;

    bool anchor_absolute;

    bool dump_headers;

    const char *session;

    /**
     * The payload of the CHECK packet.  If
     * strref_is_null(&esponse.check), then no CHECK packet was
     * received.
     */
    struct strref check;

    const char *user;
    unsigned user_max_age;

    const char *language;

    const char *realm;

    /**
     * The value of the "WWW-Authenticate" HTTP response header.
     */
    const char *www_authenticate;

    /**
     * The value of the "Authentication-Info" HTTP response header.
     */
    const char *authentication_info;

    const char *cookie_host;

    struct strmap *headers;

    struct widget_view *views;

    const uint16_t *vary;
    unsigned num_vary;

    const uint16_t *invalidate;
    unsigned num_invalidate;
};

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

void
translate(struct pool *pool, int fd,
          const struct lease *lease, void *lease_ctx,
          const struct translate_request *request,
          const struct translate_handler *handler, void *ctx,
          struct async_operation_ref *async_ref);

#endif
