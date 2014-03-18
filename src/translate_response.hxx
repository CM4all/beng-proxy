/*
 * The translation response struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_RESPONSE_HXX
#define BENG_PROXY_TRANSLATE_RESPONSE_HXX

#include "util/ConstBuffer.hxx"
#include "resource-address.h"
#include "header-forward.h"
#include "strset.h"

#include <http/status.h>

#include <glib.h>

#include <assert.h>
#include <stdint.h>

struct pool;

struct TranslateResponse {
    /**
     * The protocol version from the BEGIN packet.
     */
    unsigned protocol_version;

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

    bool unsafe_base;

    bool easy_base;

    bool regex_tail, regex_unescape;

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
     * The payload of the CHECK packet.  If ConstBuffer::IsNull(),
     * then no CHECK packet was received.
     */
    ConstBuffer<void> check;

    /**
     * The payload of the #TRANSLATE_WANT_FULL_URI packet.  If
     * ConstBuffer::IsNull(), then no #TRANSLATE_WANT_FULL_URI packet
     * was received.
     */
    ConstBuffer<void> want_full_uri;

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

    ConstBuffer<uint16_t> vary;
    ConstBuffer<uint16_t> invalidate;
    ConstBuffer<uint16_t> want;

    struct {
        uint64_t mtime;
        const char *path;
    } validate_mtime;

    void Clear();

    bool Wants(uint16_t cmd) const {
        assert(protocol_version >= 1);

        for (auto i : want)
            if (i == cmd)
                return true;

        return false;
    }
};

#ifdef __cplusplus
extern "C" {
#endif

gcc_pure
static inline bool
translate_response_vary_contains(const TranslateResponse *response,
                                 uint16_t cmd)
{
    for (auto i : response->vary)
        if (i == cmd)
            return true;

    return false;
}

void
translate_response_copy(struct pool *pool, TranslateResponse *dest,
                        const TranslateResponse *src);

/**
 * Does any response need to be expanded with
 * translate_response_expand()?
 */
gcc_pure
bool
translate_response_is_expandable(const TranslateResponse *response);

/**
 * Expand the strings in this response with the specified regex
 * result.
 */
bool
translate_response_expand(struct pool *pool,
                          TranslateResponse *response,
                          const GMatchInfo *match_info, GError **error_r);

#ifdef __cplusplus
}
#endif

#endif
