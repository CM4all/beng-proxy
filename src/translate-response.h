/*
 * The translation response struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_RESPONSE_H
#define BENG_PROXY_TRANSLATE_RESPONSE_H

#include "resource-address.h"
#include "header-forward.h"
#include "strref.h"

#include <http/status.h>

#include <stdbool.h>
#include <stdint.h>

struct pool;

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

    bool widget_info;

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

void
translate_response_copy(struct pool *pool, struct translate_response *dest,
                        const struct translate_response *src);

#endif
