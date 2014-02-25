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
#include "strset.h"

#include <http/status.h>

#include <glib.h>
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

    const char *local_uri;

    const char *untrusted;
    const char *untrusted_prefix;
    const char *untrusted_site_suffix;

    bool direct_addressing;

    bool stateful;

    bool discard_session;

    bool secure_cookie;

    bool filter_4xx;

    bool error_document;

    bool previous;

    bool transparent;

    bool auto_base;

    bool widget_info;

    bool anchor_absolute;

    bool dump_headers;

    const char *session;

    /**
     * The payload of the CHECK packet.  If
     * strref_is_null(&response.check), then no CHECK packet was
     * received.
     */
    struct strref check;

    /**
     * The payload of the #TRANSLATE_WANT_FULL_URI packet.  If
     * strref_is_null(&response.want_full_uri), then no
     * #TRANSLATE_WANT_FULL_URI packet was received.
     */
    struct strref want_full_uri;

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

    const char *cookie_domain;
    const char *cookie_host;

    struct strmap *headers;

    struct widget_view *views;

    /**
     * From #TRANSLATE_WIDGET_GROUP.
     */
    const char *widget_group;

    /**
     * From #TRANSLATE_GROUP_CONTAINER.
     */
    struct strset container_groups;

    const uint16_t *vary;
    unsigned num_vary;

    const uint16_t *invalidate;
    unsigned num_invalidate;

    struct {
        uint64_t mtime;
        const char *path;
    } validate_mtime;
};

#ifdef __cplusplus
extern "C" {
#endif

gcc_pure
static inline bool
translate_response_vary_contains(const struct translate_response *response,
                                 uint16_t cmd)
{
    for (unsigned i = 0; i < response->num_vary; ++i)
        if (response->vary[i] == cmd)
            return true;

    return false;
}

void
translate_response_copy(struct pool *pool, struct translate_response *dest,
                        const struct translate_response *src);

/**
 * Does any response need to be expanded with
 * translate_response_expand()?
 */
gcc_pure
bool
translate_response_is_expandable(const struct translate_response *response);

/**
 * Expand the strings in this response with the specified regex
 * result.
 */
bool
translate_response_expand(struct pool *pool,
                          struct translate_response *response,
                          const GMatchInfo *match_info, GError **error_r);

#ifdef __cplusplus
}
#endif

#endif
