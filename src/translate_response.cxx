/*
 * The translation response struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "translate_response.hxx"
#include "pool.hxx"
#include "pbuffer.hxx"
#include "strmap.hxx"
#include "widget_view.hxx"
#include "uri/uri_base.hxx"
#include "puri_base.hxx"
#include "puri_escape.hxx"
#include "regex.hxx"
#include "pexpand.hxx"
#include "http_address.hxx"
#include "util/StringView.hxx"

#include <glib.h>

#include <string.h>

void
TranslateResponse::Clear()
{
    protocol_version = 0;
    max_age = std::chrono::seconds(-1);
    expires_relative = std::chrono::seconds::zero();
    status = (http_status_t)0;
    address.Clear();

    request_header_forward =
        (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
            [HEADER_GROUP_CORS] = HEADER_FORWARD_NO,
            [HEADER_GROUP_SECURE] = HEADER_FORWARD_NO,
            [HEADER_GROUP_TRANSFORMATION] = HEADER_FORWARD_NO,
            [HEADER_GROUP_LINK] = HEADER_FORWARD_YES,
            [HEADER_GROUP_SSL] = HEADER_FORWARD_NO,
        },
    };

    response_header_forward =
        (struct header_forward_settings){
        .modes = {
            [HEADER_GROUP_IDENTITY] = HEADER_FORWARD_NO,
            [HEADER_GROUP_CAPABILITIES] = HEADER_FORWARD_YES,
            [HEADER_GROUP_COOKIE] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_OTHER] = HEADER_FORWARD_NO,
            [HEADER_GROUP_FORWARD] = HEADER_FORWARD_NO,
            [HEADER_GROUP_CORS] = HEADER_FORWARD_NO,
            [HEADER_GROUP_SECURE] = HEADER_FORWARD_NO,
            [HEADER_GROUP_TRANSFORMATION] = HEADER_FORWARD_MANGLE,
            [HEADER_GROUP_LINK] = HEADER_FORWARD_YES,
            [HEADER_GROUP_SSL] = HEADER_FORWARD_NO,
        },
    };

    base = nullptr;
    regex = inverse_regex = nullptr;
    site = expand_site = nullptr;
    document_root = expand_document_root = nullptr;

    redirect = expand_redirect = nullptr;
    bounce = nullptr;

    scheme = nullptr;
    host = nullptr;
    uri = expand_uri = nullptr;

    local_uri = nullptr;

    untrusted = nullptr;
    untrusted_prefix = nullptr;
    untrusted_site_suffix = nullptr;
    untrusted_raw_site_suffix = nullptr;

    test_path = expand_test_path = nullptr;
    unsafe_base = false;
    easy_base = false;
    regex_tail = regex_unescape = inverse_regex_unescape = false;
    direct_addressing = false;
    stateful = false;
    discard_session = false;
    secure_cookie = false;
    filter_4xx = false;
    previous = false;
    transparent = false;
    redirect_query_string = false;
    auto_base = false;
    widget_info = false;
    anchor_absolute = false;
    dump_headers = false;
    regex_on_host_uri = false;
    regex_on_user_uri = false;
    auto_deflate = false;
    auto_gzip = false;
    realm_from_auth_base = false;

    session = nullptr;
    internal_redirect = nullptr;
    check = nullptr;
    auth = nullptr;
    auth_file = expand_auth_file = nullptr;
    append_auth = nullptr;
    expand_append_auth = nullptr;

    want_full_uri = nullptr;

    session_site = nullptr;
    user = nullptr;
    user_max_age = std::chrono::seconds(-1);
    language = nullptr;
    realm = nullptr;

    external_session_manager = nullptr;
    external_session_keepalive = std::chrono::seconds::zero();

    www_authenticate = nullptr;
    authentication_info = nullptr;

    cookie_domain = cookie_host = expand_cookie_host = cookie_path = nullptr;

    request_headers.Clear();
    expand_request_headers.Clear();
    response_headers.Clear();
    expand_response_headers.Clear();

    views = nullptr;
    widget_group = nullptr;
    container_groups.Init();

    vary = nullptr;
    invalidate = nullptr;
    want = nullptr;
    file_not_found = nullptr;
    content_type = nullptr;
    enotdir = nullptr;
    directory_index = nullptr;
    error_document = nullptr;
    probe_path_suffixes = nullptr;
    probe_suffixes.clear();
    read_file = expand_read_file = nullptr;

    validate_mtime.mtime = 0;
    validate_mtime.path = nullptr;
}

template<unsigned N>
static void
CopyArray(struct pool &pool, TrivialArray<const char *, N> &dest,
          const TrivialArray<const char *, N> &src)
{
    const size_t size = src.size();
    dest.resize(size);
    for (size_t i = 0; i < size; ++i)
        dest[i] = p_strdup(&pool, src[i]);
}

void
TranslateResponse::CopyFrom(struct pool *pool, const TranslateResponse &src)
{
    protocol_version = src.protocol_version;

    /* we don't copy the "max_age" attribute, because it's only used
       by the tcache itself */

    expires_relative = src.expires_relative;

    status = src.status;

    request_header_forward = src.request_header_forward;
    response_header_forward = src.response_header_forward;

    base = p_strdup_checked(pool, src.base);
    regex = p_strdup_checked(pool, src.regex);
    inverse_regex = p_strdup_checked(pool, src.inverse_regex);
    site = p_strdup_checked(pool, src.site);
    expand_site = p_strdup_checked(pool, src.expand_site);
    document_root = p_strdup_checked(pool, src.document_root);
    expand_document_root = p_strdup_checked(pool, src.expand_document_root);
    redirect = p_strdup_checked(pool, src.redirect);
    expand_redirect = p_strdup_checked(pool, src.expand_redirect);
    bounce = p_strdup_checked(pool, src.bounce);
    scheme = p_strdup_checked(pool, src.scheme);
    host = p_strdup_checked(pool, src.host);
    uri = p_strdup_checked(pool, src.uri);
    expand_uri = p_strdup_checked(pool, src.expand_uri);
    local_uri = p_strdup_checked(pool, src.local_uri);
    untrusted = p_strdup_checked(pool, src.untrusted);
    untrusted_prefix = p_strdup_checked(pool, src.untrusted_prefix);
    untrusted_site_suffix =
        p_strdup_checked(pool, src.untrusted_site_suffix);
    untrusted_raw_site_suffix =
        p_strdup_checked(pool, src.untrusted_raw_site_suffix);
    unsafe_base = src.unsafe_base;
    easy_base = src.easy_base;
    regex_tail = src.regex_tail;
    regex_unescape = src.regex_unescape;
    inverse_regex_unescape = src.inverse_regex_unescape;
    direct_addressing = src.direct_addressing;
    stateful = src.stateful;
    discard_session = src.discard_session;
    secure_cookie = src.secure_cookie;
    filter_4xx = src.filter_4xx;
    previous = src.previous;
    transparent = src.transparent;
    redirect_query_string = src.redirect_query_string;
    auto_base = src.auto_base;
    widget_info = src.widget_info;
    widget_group = p_strdup_checked(pool, src.widget_group);
    test_path = p_strdup_checked(pool, src.test_path);
    expand_test_path = p_strdup_checked(pool, src.expand_test_path);
    auth_file = p_strdup_checked(pool, src.auth_file);
    expand_auth_file = p_strdup_checked(pool, src.expand_auth_file);
    append_auth = DupBuffer(*pool, src.append_auth);
    expand_append_auth = p_strdup_checked(pool, src.expand_append_auth);

    container_groups.Init();
    container_groups.CopyFrom(*pool, src.container_groups);

    anchor_absolute = src.anchor_absolute;
    dump_headers = src.dump_headers;
    regex_on_host_uri = src.regex_on_host_uri;
    regex_on_user_uri = src.regex_on_user_uri;
    auto_deflate = src.auto_deflate;
    auto_gzip = src.auto_gzip;
    realm_from_auth_base = src.realm_from_auth_base;
    session = nullptr;

    internal_redirect = DupBuffer(*pool, src.internal_redirect);
    check = DupBuffer(*pool, src.check);
    auth = DupBuffer(*pool, src.auth);
    want_full_uri = DupBuffer(*pool, src.want_full_uri);

    /* The "user" attribute must not be present in cached responses,
       because they belong to only that one session.  For the same
       reason, we won't copy the user_max_age attribute. */
    user = nullptr;
    session_site = nullptr;

    language = nullptr;
    realm = p_strdup_checked(pool, src.realm);

    external_session_manager = src.external_session_manager != nullptr
        ? http_address_dup(*pool, src.external_session_manager)
        : nullptr;
    external_session_keepalive = src.external_session_keepalive;

    www_authenticate = p_strdup_checked(pool, src.www_authenticate);
    authentication_info = p_strdup_checked(pool, src.authentication_info);
    cookie_domain = p_strdup_checked(pool, src.cookie_domain);
    cookie_host = p_strdup_checked(pool, src.cookie_host);
    expand_cookie_host = p_strdup_checked(pool, src.expand_cookie_host);
    cookie_path = p_strdup_checked(pool, src.cookie_path);

    request_headers = KeyValueList(PoolAllocator(*pool), src.request_headers);
    expand_request_headers = KeyValueList(PoolAllocator(*pool),
                                          src.expand_request_headers);
    response_headers = KeyValueList(PoolAllocator(*pool), src.response_headers);
    expand_response_headers = KeyValueList(PoolAllocator(*pool),
                                           src.expand_response_headers);

    views = src.views != nullptr
        ? src.views->CloneChain(*pool)
        : nullptr;

    vary = DupBuffer(*pool, src.vary);
    invalidate = DupBuffer(*pool, src.invalidate);
    want = DupBuffer(*pool, src.want);
    file_not_found = DupBuffer(*pool, src.file_not_found);
    content_type = p_strdup_checked(pool, src.content_type);
    enotdir = DupBuffer(*pool, src.enotdir);
    directory_index = DupBuffer(*pool, src.directory_index);
    error_document = DupBuffer(*pool, src.error_document);
    probe_path_suffixes = DupBuffer(*pool, src.probe_path_suffixes);
    CopyArray(*pool, probe_suffixes, src.probe_suffixes);
    read_file = p_strdup_checked(pool, src.read_file);
    expand_read_file = p_strdup_checked(pool, src.expand_read_file);

    validate_mtime.mtime = src.validate_mtime.mtime;
    validate_mtime.path =
        p_strdup_checked(pool, src.validate_mtime.path);
}

bool
TranslateResponse::CacheStore(struct pool *pool, const TranslateResponse &src,
                              const char *request_uri)
{
    CopyFrom(pool, src);

    char *new_base = nullptr;
    if (auto_base) {
        assert(base == nullptr);
        assert(request_uri != nullptr);

        base = new_base = src.address.AutoBase(*pool, request_uri);
    }

    const bool expandable = src.IsExpandable();

    const bool has_base = address.CacheStore(*pool, src.address,
                                             request_uri, base,
                                             easy_base,
                                             expandable);

    if (!has_base)
        /* the BASE value didn't match - clear it */
        base = nullptr;
    else if (new_base != nullptr)
        base = new_base;

    if (base != nullptr && !expandable && !easy_base) {
        const char *tail = base_tail(request_uri, base);
        if (tail != nullptr) {
            if (uri != nullptr) {
                size_t length = base_string(uri, tail);
                uri = length != (size_t)-1
                    ? p_strndup(pool, uri, length)
                    : nullptr;

                if (uri == nullptr && !internal_redirect.IsNull())
                    /* this BASE mismatch is fatal, because it
                       invalidates a required attribute; clearing
                       "base" is an trigger for tcache_store() to fail
                       on this translation response */
                    // TODO: throw an exception instead of this kludge
                    base = nullptr;
            }

            if (redirect != nullptr) {
                size_t length = base_string(redirect, tail);
                redirect = length != (size_t)-1
                    ? p_strndup(pool, redirect, length)
                    : nullptr;
            }

            if (test_path != nullptr) {
                size_t length = base_string_unescape(pool, test_path, tail);
                test_path = length != (size_t)-1
                    ? p_strndup(pool, test_path, length)
                    : nullptr;
            }
        }
    }

    return has_base;
}

bool
TranslateResponse::CacheLoad(struct pool *pool, const TranslateResponse &src,
                             const char *request_uri, GError **error_r)
{
    const bool expandable = src.IsExpandable();

    if (!address.CacheLoad(*pool, src.address, request_uri, src.base,
                           src.unsafe_base, expandable, error_r))
        return false;

    if (this != &src)
        CopyFrom(pool, src);

    if (base != nullptr && !expandable) {
        const char *tail = require_base_tail(request_uri, base);

        if (uri != nullptr)
            uri = p_strcat(pool, uri, tail, nullptr);

        if (redirect != nullptr)
            redirect = p_strcat(pool, redirect, tail, nullptr);

        if (test_path != nullptr) {
            char *unescaped = uri_unescape_dup(pool, tail);
            if (unescaped == nullptr)
                return false;

            test_path = p_strcat(pool, test_path, unescaped, nullptr);
        }
    }

    return true;
}

UniqueRegex
TranslateResponse::CompileRegex(Error &error_r) const
{
    assert(regex != nullptr);

    UniqueRegex r;
    r.Compile(regex, protocol_version >= 3, IsExpandable(), error_r);
    return r;
}

UniqueRegex
TranslateResponse::CompileInverseRegex(Error &error_r) const
{
    assert(inverse_regex != nullptr);

    UniqueRegex r;
    r.Compile(inverse_regex, protocol_version >= 3, false, error_r);
    return r;
}

bool
TranslateResponse::IsExpandable() const
{
    return regex != nullptr &&
        (expand_redirect != nullptr ||
         expand_site != nullptr ||
         expand_document_root != nullptr ||
         expand_uri != nullptr ||
         expand_test_path != nullptr ||
         expand_auth_file != nullptr ||
         expand_read_file != nullptr ||
         expand_append_auth != nullptr ||
         expand_cookie_host != nullptr ||
         !expand_request_headers.IsEmpty() ||
         !expand_response_headers.IsEmpty() ||
         address.IsExpandable() ||
         (external_session_manager != nullptr &&
          external_session_manager->IsExpandable()) ||
         widget_view_any_is_expandable(views));
}

bool
TranslateResponse::Expand(struct pool *pool,
                          const MatchInfo &match_info, Error &error_r)
{
    assert(pool != nullptr);
    assert(regex != nullptr);

    if (expand_redirect != nullptr) {
        redirect = expand_string_unescaped(pool, expand_redirect,
                                           match_info, error_r);
        if (redirect == nullptr)
            return false;
    }

    if (expand_site != nullptr) {
        site = expand_string_unescaped(pool, expand_site, match_info, error_r);
        if (site == nullptr)
            return false;
    }

    if (expand_document_root != nullptr) {
        document_root = expand_string_unescaped(pool, expand_document_root,
                                                match_info, error_r);
        if (document_root == nullptr)
            return false;
    }

    if (expand_uri != nullptr) {
        uri = expand_string_unescaped(pool, expand_uri, match_info, error_r);
        if (uri == nullptr)
            return false;
    }

    if (expand_test_path != nullptr) {
        test_path = expand_string_unescaped(pool, expand_test_path,
                                            match_info, error_r);
        if (test_path == nullptr)
            return false;
    }

    if (expand_auth_file != nullptr) {
        auth_file = expand_string_unescaped(pool, expand_auth_file,
                                            match_info, error_r);
        if (auth_file == nullptr)
            return false;
    }

    if (expand_read_file != nullptr) {
        read_file = expand_string_unescaped(pool, expand_read_file,
                                            match_info, error_r);
        if (read_file == nullptr)
            return false;
    }

    if (expand_append_auth != nullptr) {
        const char *value = expand_string_unescaped(pool, expand_append_auth,
                                                    match_info, error_r);
        if (auth_file == nullptr)
            return false;

        append_auth = { value, strlen(value) };
    }

    if (expand_cookie_host != nullptr) {
        cookie_host = expand_string_unescaped(pool, expand_cookie_host,
                                              match_info, error_r);
        if (cookie_host == nullptr)
            return false;
    }

    for (const auto &i : expand_request_headers) {
        const char *value = expand_string_unescaped(pool, i.value,
                                                    match_info, error_r);
        if (value == nullptr)
            return false;

        request_headers.Add(PoolAllocator(*pool), i.key, value);
    }

    for (const auto &i : expand_response_headers) {
        const char *value = expand_string_unescaped(pool, i.value,
                                                    match_info, error_r);
        if (value == nullptr)
            return false;

        response_headers.Add(PoolAllocator(*pool), i.key, value);
    }

    return address.Expand(*pool, match_info, error_r) &&
        (external_session_manager == nullptr ||
         external_session_manager->Expand(pool, match_info, error_r)) &&
        widget_view_expand_all(pool, views, match_info, error_r);
}
