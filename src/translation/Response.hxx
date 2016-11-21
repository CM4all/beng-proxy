/*
 * The translation response struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_TRANSLATE_RESPONSE_HXX
#define BENG_PROXY_TRANSLATE_RESPONSE_HXX

#include "Features.hxx"
#include "util/ConstBuffer.hxx"
#include "util/TrivialArray.hxx"
#if TRANSLATION_ENABLE_HTTP
#include "util/kvlist.hxx"
#include "header_forward.hxx"
#endif
#if TRANSLATION_ENABLE_WIDGET
#include "util/StringSet.hxx"
#endif
#if TRANSLATION_ENABLE_RADDRESS
#include "ResourceAddress.hxx"
#include "header_forward.hxx"
#endif

#include <http/status.h>

#include <chrono>

#include <assert.h>
#include <stdint.h>

struct WidgetView;
class AllocatorPtr;
class UniqueRegex;
class MatchInfo;

struct TranslateResponse {
    /**
     * The protocol version from the BEGIN packet.
     */
    unsigned protocol_version;

    std::chrono::seconds max_age;

    /**
     * From #TRANSLATE_EXPIRES_RELATIVE
     */
    std::chrono::seconds expires_relative;

#if TRANSLATION_ENABLE_HTTP
    http_status_t status;
#else
    int status;
#endif

#if TRANSLATION_ENABLE_RADDRESS
    ResourceAddress address;
#endif

#if TRANSLATION_ENABLE_HTTP
    /**
     * Which request headers are forwarded?
     */
    struct header_forward_settings request_header_forward;

    /**
     * Which response headers are forwarded?
     */
    struct header_forward_settings response_header_forward;
#endif

    const char *base;

#if TRANSLATION_ENABLE_CACHE
    const char *regex;
    const char *inverse_regex;
#endif

    const char *site;
    const char *expand_site;

#if TRANSLATION_ENABLE_HTTP
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
    const char *untrusted_raw_site_suffix;
#endif

    /**
     * @see #TRANSLATE_TEST_PATH
     */
    const char *test_path;

    /**
     * @see #TRANSLATE_EXPAND_TEST_PATH
     */
    const char *expand_test_path;

#if TRANSLATION_ENABLE_RADDRESS
    bool unsafe_base;

    bool easy_base;
#endif

#if TRANSLATION_ENABLE_CACHE
    bool regex_tail, regex_unescape, inverse_regex_unescape;
#endif

#if TRANSLATION_ENABLE_WIDGET
    bool direct_addressing;
#endif

#if TRANSLATION_ENABLE_SESSION
    bool stateful;

    bool discard_session;

    bool secure_cookie;
#endif

#if TRANSLATION_ENABLE_TRANSFORMATION
    bool filter_4xx;
#endif

    bool previous;

    bool transparent;

#if TRANSLATION_ENABLE_HTTP
    bool redirect_query_string;
#endif

#if TRANSLATION_ENABLE_RADDRESS
    bool auto_base;
#endif

#if TRANSLATION_ENABLE_WIDGET
    bool widget_info;

    bool anchor_absolute;
#endif

#if TRANSLATION_ENABLE_HTTP
    bool dump_headers;
#endif

#if TRANSLATION_ENABLE_CACHE
    /**
     * @see #TRANSLATE_REGEX_ON_HOST_URI
     */
    bool regex_on_host_uri;

    /**
     * @see #TRANSLATE_REGEX_ON_USER_URI
     */
    bool regex_on_user_uri;
#endif

    /**
     * @see #TRANSLATE_AUTO_DEFLATE
     */
    bool auto_deflate;

    /**
     * @see #TRANSLATE_AUTO_GZIP
     */
    bool auto_gzip;

#if TRANSLATION_ENABLE_SESSION
    /**
     * @see #TRANSLATE_REALM_FROM_AUTH_BASE
     */
    bool realm_from_auth_base;

    ConstBuffer<void> session;
#endif

#if TRANSLATION_ENABLE_HTTP
    /**
     * The payload of the #TRANSLATE_INTERNAL_REDIRECT packet.  If
     * ConstBuffer::IsNull(), then no #TRANSLATE_INTERNAL_REDIRECT
     * packet was received.
     */
    ConstBuffer<void> internal_redirect;
#endif

#if TRANSLATION_ENABLE_SESSION
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
#endif

#if TRANSLATION_ENABLE_HTTP
    /**
     * The payload of the #TRANSLATE_WANT_FULL_URI packet.  If
     * ConstBuffer::IsNull(), then no #TRANSLATE_WANT_FULL_URI packet
     * was received.
     */
    ConstBuffer<void> want_full_uri;
#endif

#if TRANSLATION_ENABLE_SESSION
    const char *user;
    std::chrono::seconds user_max_age;

    const char *session_site;

    const char *language;

    const char *realm;

    HttpAddress *external_session_manager;
    std::chrono::duration<uint16_t> external_session_keepalive;

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
#endif

#if TRANSLATION_ENABLE_HTTP
    KeyValueList request_headers;
    KeyValueList expand_request_headers;

    KeyValueList response_headers;
    KeyValueList expand_response_headers;
#endif

#if TRANSLATION_ENABLE_WIDGET
    WidgetView *views;

    /**
     * From #TRANSLATE_WIDGET_GROUP.
     */
    const char *widget_group;

    /**
     * From #TRANSLATE_GROUP_CONTAINER.
     */
    StringSet container_groups;
#endif

#if TRANSLATION_ENABLE_CACHE
    ConstBuffer<uint16_t> vary;
    ConstBuffer<uint16_t> invalidate;
#endif
    ConstBuffer<uint16_t> want;

#if TRANSLATION_ENABLE_RADDRESS
    ConstBuffer<void> file_not_found;

    /**
     * From #TRANSLATE_CONTENT_TYPE, but only in reply to
     * #TRANSLATE_CONTENT_TYPE_LOOKUP / #TRANSLATE_SUFFIX.
     */
    const char *content_type;

    ConstBuffer<void> enotdir;

    ConstBuffer<void> directory_index;
#endif

    ConstBuffer<void> error_document;

    /**
     * From #TRANSLATE_PROBE_PATH_SUFFIXES.
     */
    ConstBuffer<void> probe_path_suffixes;

    TrivialArray<const char *, 16> probe_suffixes;

    const char *read_file, *expand_read_file;

    struct {
        uint64_t mtime;
        const char *path;
    } validate_mtime;

    void Clear();

    bool Wants(uint16_t cmd) const {
        assert(protocol_version >= 1);

        return want.Contains(cmd);
    }

#if TRANSLATION_ENABLE_CACHE
    gcc_pure
    bool VaryContains(uint16_t cmd) const {
        return vary.Contains(cmd);
    }
#endif

#if TRANSLATION_ENABLE_SESSION
    gcc_pure
    bool HasAuth() const {
        return !auth.IsNull() ||
            auth_file != nullptr || expand_auth_file != nullptr;
    }

    bool HasUntrusted() const {
        return untrusted != nullptr || untrusted_prefix != nullptr ||
            untrusted_site_suffix != nullptr ||
            untrusted_raw_site_suffix != nullptr;
    }
#endif

    void CopyFrom(AllocatorPtr alloc, const TranslateResponse &src);

#if TRANSLATION_ENABLE_CACHE
    /**
     * Copy data from #src for storing in the translation cache.
     *
     * @return true if a #base was given and it was applied
     * successfully
     */
    bool CacheStore(AllocatorPtr alloc, const TranslateResponse &src,
                    const char *uri);

    /**
     * Throws std::runtime_error on error.
     */
    void CacheLoad(AllocatorPtr alloc, const TranslateResponse &src,
                   const char *uri);
#endif

    /**
     * Throws std::runtime_error on error.
     */
    UniqueRegex CompileRegex() const;
    UniqueRegex CompileInverseRegex() const;

#if TRANSLATION_ENABLE_EXPAND
    /**
     * Does any response need to be expanded with
     * translate_response_expand()?
     */
    gcc_pure
    bool IsExpandable() const;

    /**
     * Expand the strings in this response with the specified regex
     * result.
     *
     * Throws std::runtime_error on error.
     */
    void Expand(struct pool *pool, const MatchInfo &match_info);
#endif
};

#endif
