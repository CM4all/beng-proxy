/*
 * The translation response struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_RESPONSE_HXX
#define BENG_PROXY_TRANSLATE_RESPONSE_HXX

#include "util/ConstBuffer.hxx"
#include "util/TrivialArray.hxx"
#include "util/kvlist.hxx"
#include "resource_address.hxx"
#include "header_forward.hxx"
#include "strset.h"
#include "glibfwd.hxx"

#include <http/status.h>

#include <assert.h>
#include <stdint.h>

struct pool;
struct WidgetView;

struct TranslateResponse {
    /**
     * The protocol version from the BEGIN packet.
     */
    unsigned protocol_version;

    unsigned max_age;

    /**
     * From #TRANSLATE_EXPIRES_RELATIVE
     */
    unsigned expires_relative;

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
    const char *expand_site;
    const char *document_root;

    /**
     * The value of #TRANSLATE_EXPAND_DOCUMENT_ROOT.  Only used by the
     * translation cache.
     */
    const char *expand_document_root;

    const char *redirect;
    const char *expand_redirect;
    const char *bounce;

    const char *scheme;
    const char *host;
    const char *uri;
    const char *expand_uri;

    const char *local_uri;

    const char *untrusted;
    const char *untrusted_prefix;
    const char *untrusted_site_suffix;

    /**
     * @see #TRANSLATE_TEST_PATH
     */
    const char *test_path;

    /**
     * @see #TRANSLATE_EXPAND_TEST_PATH
     */
    const char *expand_test_path;

    bool unsafe_base;

    bool easy_base;

    bool regex_tail, regex_unescape;

    bool direct_addressing;

    bool stateful;

    bool discard_session;

    bool secure_cookie;

    bool filter_4xx;

    bool previous;

    bool transparent;

    bool redirect_query_string;

    bool auto_base;

    bool widget_info;

    bool anchor_absolute;

    bool dump_headers;

    ConstBuffer<void> session;

    /**
     * The payload of the CHECK packet.  If ConstBuffer::IsNull(),
     * then no CHECK packet was received.
     */
    ConstBuffer<void> check;

    /**
     * The payload of the AUTH packet.  If ConstBuffer::IsNull(), then
     * no AUTH packet was received.
     */
    ConstBuffer<void> auth;

    /**
     * @see #TRANSLATE_AUTH_FILE, #TRANSLATE_EXPAND_AUTH_FILE
     */
    const char *auth_file, *expand_auth_file;

    /**
     * @see #TRANSLATE_APPEND_AUTH
     */
    ConstBuffer<void> append_auth;

    /**
     * @see #TRANSLATE_EXPAND_APPEND_AUTH
     */
    const char *expand_append_auth;

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
    const char *cookie_host, *expand_cookie_host;
    const char *cookie_path;

    KeyValueList request_headers;
    KeyValueList expand_request_headers;

    KeyValueList response_headers;

    WidgetView *views;

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

    ConstBuffer<void> file_not_found;

    ConstBuffer<void> content_type_lookup;

    /**
     * From #TRANSLATE_CONTENT_TYPE, but only in reply to
     * #TRANSLATE_CONTENT_TYPE_LOOKUP / #TRANSLATE_SUFFIX.
     */
    const char *content_type;

    ConstBuffer<void> enotdir;

    ConstBuffer<void> directory_index;

    ConstBuffer<void> error_document;

    /**
     * From #TRANSLATE_PROBE_PATH_SUFFIXES.
     */
    ConstBuffer<void> probe_path_suffixes;

    TrivialArray<const char *, 16> probe_suffixes;

    const char *read_file;

    struct {
        uint64_t mtime;
        const char *path;
    } validate_mtime;

    void Clear();

    bool Wants(uint16_t cmd) const {
        assert(protocol_version >= 1);

        return want.Contains(cmd);
    }

    gcc_pure
    bool VaryContains(uint16_t cmd) const {
        return vary.Contains(cmd);
    }

    gcc_pure
    bool HasAuth() const {
        return !auth.IsNull() ||
            auth_file != nullptr || expand_auth_file != nullptr;
    }

    void CopyFrom(struct pool *pool, const TranslateResponse &src);

    /**
     * Copy data from #src for storing in the translation cache.
     *
     * @return true if a #base was given and it was applied
     * successfully
     */
    bool CacheStore(struct pool *pool, const TranslateResponse &src,
                    const char *uri);

    bool CacheLoad(struct pool *pool, const TranslateResponse &src,
                   const char *uri, GError **error_r);

    GRegex *CompileRegex(GError **error_r) const;
    GRegex *CompileInverseRegex(GError **error_r) const;

    /**
     * Does any response need to be expanded with
     * translate_response_expand()?
     */
    gcc_pure
    bool IsExpandable() const;

    /**
     * Expand the strings in this response with the specified regex
     * result.
     */
    bool Expand(struct pool *pool,
                const GMatchInfo *match_info, GError **error_r);
};

#endif
