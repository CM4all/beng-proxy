/*
 * The translation response struct.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "Response.hxx"
#include "widget_view.hxx"
#include "uri/uri_base.hxx"
#include "puri_base.hxx"
#include "puri_escape.hxx"
#include "AllocatorPtr.hxx"
#include "regex.hxx"
#include "pexpand.hxx"
#include "http_address.hxx"
#include "HttpMessageResponse.hxx"
#include "util/StringView.hxx"

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
CopyArray(AllocatorPtr alloc, TrivialArray<const char *, N> &dest,
          const TrivialArray<const char *, N> &src)
{
    const size_t size = src.size();
    dest.resize(size);
    for (size_t i = 0; i < size; ++i)
        dest[i] = alloc.Dup(src[i]);
}

void
TranslateResponse::CopyFrom(AllocatorPtr alloc, const TranslateResponse &src)
{
    protocol_version = src.protocol_version;

    /* we don't copy the "max_age" attribute, because it's only used
       by the tcache itself */

    expires_relative = src.expires_relative;

    status = src.status;

    request_header_forward = src.request_header_forward;
    response_header_forward = src.response_header_forward;

    base = alloc.CheckDup(src.base);
    regex = alloc.CheckDup(src.regex);
    inverse_regex = alloc.CheckDup(src.inverse_regex);
    site = alloc.CheckDup(src.site);
    expand_site = alloc.CheckDup(src.expand_site);
    document_root = alloc.CheckDup(src.document_root);
    expand_document_root = alloc.CheckDup(src.expand_document_root);
    redirect = alloc.CheckDup(src.redirect);
    expand_redirect = alloc.CheckDup(src.expand_redirect);
    bounce = alloc.CheckDup(src.bounce);
    scheme = alloc.CheckDup(src.scheme);
    host = alloc.CheckDup(src.host);
    uri = alloc.CheckDup(src.uri);
    expand_uri = alloc.CheckDup(src.expand_uri);
    local_uri = alloc.CheckDup(src.local_uri);
    untrusted = alloc.CheckDup(src.untrusted);
    untrusted_prefix = alloc.CheckDup(src.untrusted_prefix);
    untrusted_site_suffix =
        alloc.CheckDup(src.untrusted_site_suffix);
    untrusted_raw_site_suffix =
        alloc.CheckDup(src.untrusted_raw_site_suffix);
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
    widget_group = alloc.CheckDup(src.widget_group);
    test_path = alloc.CheckDup(src.test_path);
    expand_test_path = alloc.CheckDup(src.expand_test_path);
    auth_file = alloc.CheckDup(src.auth_file);
    expand_auth_file = alloc.CheckDup(src.expand_auth_file);
    append_auth = alloc.Dup(src.append_auth);
    expand_append_auth = alloc.CheckDup(src.expand_append_auth);

    container_groups.Init();
    container_groups.CopyFrom(alloc, src.container_groups);

    anchor_absolute = src.anchor_absolute;
    dump_headers = src.dump_headers;
    regex_on_host_uri = src.regex_on_host_uri;
    regex_on_user_uri = src.regex_on_user_uri;
    auto_deflate = src.auto_deflate;
    auto_gzip = src.auto_gzip;
    realm_from_auth_base = src.realm_from_auth_base;
    session = nullptr;

    internal_redirect = alloc.Dup(src.internal_redirect);
    check = alloc.Dup(src.check);
    auth = alloc.Dup(src.auth);
    want_full_uri = alloc.Dup(src.want_full_uri);

    /* The "user" attribute must not be present in cached responses,
       because they belong to only that one session.  For the same
       reason, we won't copy the user_max_age attribute. */
    user = nullptr;
    session_site = nullptr;

    language = nullptr;
    realm = alloc.CheckDup(src.realm);

    external_session_manager = src.external_session_manager != nullptr
        ? alloc.New<HttpAddress>(alloc, *src.external_session_manager)
        : nullptr;
    external_session_keepalive = src.external_session_keepalive;

    www_authenticate = alloc.CheckDup(src.www_authenticate);
    authentication_info = alloc.CheckDup(src.authentication_info);
    cookie_domain = alloc.CheckDup(src.cookie_domain);
    cookie_host = alloc.CheckDup(src.cookie_host);
    expand_cookie_host = alloc.CheckDup(src.expand_cookie_host);
    cookie_path = alloc.CheckDup(src.cookie_path);

    request_headers = KeyValueList(alloc, src.request_headers);
    expand_request_headers = KeyValueList(alloc, src.expand_request_headers);
    response_headers = KeyValueList(alloc, src.response_headers);
    expand_response_headers = KeyValueList(alloc, src.expand_response_headers);

    views = src.views != nullptr
        ? src.views->CloneChain(alloc)
        : nullptr;

    vary = alloc.Dup(src.vary);
    invalidate = alloc.Dup(src.invalidate);
    want = alloc.Dup(src.want);
    file_not_found = alloc.Dup(src.file_not_found);
    content_type = alloc.CheckDup(src.content_type);
    enotdir = alloc.Dup(src.enotdir);
    directory_index = alloc.Dup(src.directory_index);
    error_document = alloc.Dup(src.error_document);
    probe_path_suffixes = alloc.Dup(src.probe_path_suffixes);
    CopyArray(alloc, probe_suffixes, src.probe_suffixes);
    read_file = alloc.CheckDup(src.read_file);
    expand_read_file = alloc.CheckDup(src.expand_read_file);

    validate_mtime.mtime = src.validate_mtime.mtime;
    validate_mtime.path = alloc.CheckDup(src.validate_mtime.path);
}

bool
TranslateResponse::CacheStore(AllocatorPtr alloc, const TranslateResponse &src,
                              const char *request_uri)
{
    CopyFrom(alloc, src);

    const char *new_base = nullptr;
    if (auto_base) {
        assert(base == nullptr);
        assert(request_uri != nullptr);

        base = new_base = src.address.AutoBase(alloc, request_uri);
    }

    const bool expandable = src.IsExpandable();

    const bool has_base = address.CacheStore(alloc, src.address,
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
                    ? alloc.DupZ({uri, length})
                    : nullptr;

                if (uri == nullptr && !internal_redirect.IsNull())
                    /* this BASE mismatch is fatal, because it
                       invalidates a required attribute; clearing
                       "base" is an trigger for tcache_store() to fail
                       on this translation response */
                    // TODO: throw an exception instead of this kludge
                    base = nullptr;
            }

            if (test_path != nullptr) {
                size_t length = base_string_unescape(alloc, test_path, tail);
                test_path = length != (size_t)-1
                    ? alloc.DupZ({test_path, length})
                    : nullptr;
            }
        }
    }

    return has_base;
}

void
TranslateResponse::CacheLoad(AllocatorPtr alloc, const TranslateResponse &src,
                             const char *request_uri)
{
    const bool expandable = src.IsExpandable();

    address.CacheLoad(alloc, src.address, request_uri, src.base,
                      src.unsafe_base, expandable);

    if (this != &src)
        CopyFrom(alloc, src);

    if (base != nullptr && !expandable) {
        const char *tail = require_base_tail(request_uri, base);

        if (uri != nullptr)
            uri = alloc.Concat(uri, tail);

        if (test_path != nullptr) {
            char *unescaped = uri_unescape_dup(alloc, tail);
            if (unescaped == nullptr)
                throw HttpMessageResponse(HTTP_STATUS_BAD_REQUEST, "Malformed URI tail");

            test_path = alloc.Concat(test_path, unescaped);
        }
    }
}

UniqueRegex
TranslateResponse::CompileRegex() const
{
    assert(regex != nullptr);

    UniqueRegex r;
    r.Compile(regex, protocol_version >= 3, IsExpandable());
    return r;
}

UniqueRegex
TranslateResponse::CompileInverseRegex() const
{
    assert(inverse_regex != nullptr);

    UniqueRegex r;
    r.Compile(inverse_regex, protocol_version >= 3, false);
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

void
TranslateResponse::Expand(struct pool *pool, const MatchInfo &match_info)
{
    assert(pool != nullptr);
    assert(regex != nullptr);

    if (expand_redirect != nullptr)
        redirect = expand_string_unescaped(pool, expand_redirect, match_info);

    if (expand_site != nullptr)
        site = expand_string_unescaped(pool, expand_site, match_info);

    if (expand_document_root != nullptr)
        document_root = expand_string_unescaped(pool, expand_document_root,
                                                match_info);

    if (expand_uri != nullptr)
        uri = expand_string_unescaped(pool, expand_uri, match_info);

    if (expand_test_path != nullptr)
        test_path = expand_string_unescaped(pool, expand_test_path,
                                            match_info);

    if (expand_auth_file != nullptr)
        auth_file = expand_string_unescaped(pool, expand_auth_file,
                                            match_info);

    if (expand_read_file != nullptr)
        read_file = expand_string_unescaped(pool, expand_read_file,
                                            match_info);

    if (expand_append_auth != nullptr) {
        const char *value = expand_string_unescaped(pool, expand_append_auth,
                                                    match_info);
        append_auth = { value, strlen(value) };
    }

    if (expand_cookie_host != nullptr)
        cookie_host = expand_string_unescaped(pool, expand_cookie_host,
                                              match_info);

    for (const auto &i : expand_request_headers) {
        const char *value = expand_string_unescaped(pool, i.value, match_info);
        request_headers.Add(PoolAllocator(*pool), i.key, value);
    }

    for (const auto &i : expand_response_headers) {
        const char *value = expand_string_unescaped(pool, i.value, match_info);
        response_headers.Add(PoolAllocator(*pool), i.key, value);
    }

    address.Expand(*pool, match_info);

    if (external_session_manager != nullptr)
        external_session_manager->Expand(pool, match_info);

    widget_view_expand_all(pool, views, match_info);
}
