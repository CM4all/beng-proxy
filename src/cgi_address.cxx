/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "cgi_address.hxx"
#include "pool.hxx"
#include "uri/uri_base.hxx"
#include "uri/uri_escape.hxx"
#include "uri/uri_extract.hxx"
#include "util/StringView.hxx"
#include "puri_relative.hxx"
#include "puri_base.hxx"
#include "puri_escape.hxx"
#include "puri_edit.hxx"
#include "pexpand.hxx"

#include <string.h>

CgiAddress::CgiAddress(const char *_path)
    :path(_path)
{
}

CgiAddress *
cgi_address_new(struct pool &pool, const char *path)
{
    return NewFromPool<CgiAddress>(pool, path);
}

CgiAddress::CgiAddress(struct pool &pool, const CgiAddress &src)
    :path(p_strdup(&pool, src.path)),
     args(pool, src.args),
     params(pool, src.params),
     options(&pool, src.options),
     interpreter(p_strdup_checked(&pool, src.interpreter)),
     action(p_strdup_checked(&pool, src.action)),
     uri(p_strdup_checked(&pool, src.uri)),
     script_name(p_strdup_checked(&pool, src.script_name)),
     path_info(p_strdup_checked(&pool, src.path_info)),
     query_string(p_strdup_checked(&pool, src.query_string)),
     document_root(p_strdup_checked(&pool, src.document_root)),
     expand_path(p_strdup_checked(&pool, src.expand_path)),
     expand_uri(p_strdup_checked(&pool, src.expand_uri)),
     expand_script_name(p_strdup_checked(&pool, src.expand_script_name)),
     expand_path_info(p_strdup_checked(&pool, src.expand_path_info)),
     expand_document_root(p_strdup_checked(&pool, src.expand_document_root)),
     address_list(pool, src.address_list)
{
}

gcc_pure
static bool
HasTrailingSlash(const char *p)
{
    size_t length = strlen(p);
    return length > 0 && p[length - 1] == '/';
}

const char *
CgiAddress::GetURI(struct pool *pool) const
{
    if (uri != nullptr)
        return uri;

    const char *sn = script_name;
    if (sn == nullptr)
        sn = "/";

    const char *pi = path_info;
    const char *qm = nullptr;
    const char *qs = query_string;

    if (pi == nullptr) {
        if (qs == nullptr)
            return sn;

        pi = "";
    }

    if (qs != nullptr)
        qm = "?";

    if (*pi == '/' && HasTrailingSlash(sn))
        /* avoid generating a double slash when concatenating
           script_name and path_info */
        ++pi;

    return p_strcat(pool, sn, pi, qm, qs, nullptr);
}

const char *
CgiAddress::GetId(struct pool *pool) const
{
    char child_options_buffer[4096];
    *options.MakeId(child_options_buffer) = 0;

    const char *p = p_strcat(pool, path,
                             child_options_buffer,
                             nullptr);

    if (document_root != nullptr)
        p = p_strcat(pool, p, ";d=", document_root, nullptr);

    if (interpreter != nullptr)
        p = p_strcat(pool, p, ";i=", interpreter, nullptr);

    if (action != nullptr)
        p = p_strcat(pool, p, ";a=", action, nullptr);

    for (auto i : args)
        p = p_strcat(pool, p, "!", i, nullptr);

    for (auto i : params)
        p = p_strcat(pool, p, "~", i, nullptr);

    if (uri != nullptr)
        p = p_strcat(pool, p, ";u=", uri, nullptr);
    else if (script_name != nullptr)
        p = p_strcat(pool, p, ";s=", script_name, nullptr);

    if (path_info != nullptr)
        p = p_strcat(pool, p, ";p=", path_info, nullptr);

    if (query_string != nullptr)
        p = p_strcat(pool, p, "?", query_string, nullptr);

    return p;
}

CgiAddress *
CgiAddress::Clone(struct pool &p) const
{
    return NewFromPool<CgiAddress>(p, p, *this);
}

void
CgiAddress::InsertQueryString(struct pool &pool, const char *new_query_string)
{
    if (query_string != nullptr)
        query_string = p_strcat(&pool, new_query_string, "&",
                                query_string, nullptr);
    else
        query_string = p_strdup(&pool, new_query_string);
}

void
CgiAddress::InsertArgs(struct pool &pool, StringView new_args,
                       StringView new_path_info)
{
    uri = uri_insert_args(&pool, uri, new_args, new_path_info);

    if (path_info != nullptr)
        path_info = p_strncat(&pool,
                              path_info, strlen(path_info),
                              ";", (size_t)1, new_args.data, new_args.size,
                              new_path_info.data, new_path_info.size,
                              nullptr);
}

bool
CgiAddress::IsValidBase() const
{
    return IsExpandable() || (path_info != nullptr && is_base(path_info));
}

char *
CgiAddress::AutoBase(struct pool *pool, const char *request_uri) const
{
    /* auto-generate the BASE only if the path info begins with a
       slash and matches the URI */

    if (path_info == nullptr ||
        path_info[0] != '/' ||
        path_info[1] == 0)
        return nullptr;

    /* XXX implement (un-)escaping of the uri */

    size_t length = base_string(request_uri, path_info + 1);
    if (length == 0 || length == (size_t)-1)
        return nullptr;

    return p_strndup(pool, request_uri, length);
}

CgiAddress *
CgiAddress::SaveBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    size_t uri_length = uri != nullptr
        ? base_string_unescape(pool, uri, suffix)
        : 0;
    if (uri_length == (size_t)-1)
        return nullptr;

    const char *new_path_info = path_info != nullptr ? path_info : "";
    size_t length = base_string_unescape(pool, new_path_info, suffix);
    if (length == (size_t)-1)
        return nullptr;

    CgiAddress *dest = Clone(*pool);
    if (dest->uri != nullptr)
        dest->uri = p_strndup(pool, dest->uri, uri_length);
    dest->path_info = p_strndup(pool, new_path_info, length);
    return dest;
}

CgiAddress *
CgiAddress::LoadBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    char *unescaped = uri_unescape_dup(pool, suffix);
    if (unescaped == nullptr)
        return nullptr;

    CgiAddress *dest = Clone(*pool);
    if (dest->uri != nullptr)
        dest->uri = p_strcat(pool, dest->uri, unescaped, nullptr);

    const char *new_path_info = path_info != nullptr ? path_info : "";
    dest->path_info = p_strcat(pool, new_path_info, unescaped, nullptr);
    return dest;
}

const CgiAddress *
CgiAddress::Apply(struct pool *pool,
                  StringView relative) const
{
    if (relative.IsEmpty())
        return this;

    if (uri_has_authority(relative))
        return nullptr;

    char *unescaped = (char *)p_malloc(pool, relative.size);
    char *unescaped_end = uri_unescape(unescaped, relative);
    if (unescaped_end == nullptr)
        return nullptr;

    size_t unescaped_length = unescaped_end - unescaped;

    const char *new_path_info = path_info != nullptr ? path_info : "";

    auto *dest = NewFromPool<CgiAddress>(*pool, ShallowCopy(), *this);
    dest->path_info = uri_absolute(pool, new_path_info,
                                   {unescaped, unescaped_length});
    assert(dest->path_info != nullptr);
    return dest;
}

bool
CgiAddress::Expand(struct pool *pool, const MatchInfo &match_info,
                   Error &error_r)
{
    assert(pool != nullptr);

    if (!options.Expand(*pool, match_info, error_r))
        return false;

    if (expand_path != nullptr) {
        path = expand_string_unescaped(pool, expand_path, match_info, error_r);
        if (path == nullptr)
            return false;
    }

    if (expand_uri != nullptr) {
        uri = expand_string_unescaped(pool, expand_uri, match_info, error_r);
        if (uri == nullptr)
            return false;
    }

    if (expand_script_name != nullptr) {
        script_name = expand_string_unescaped(pool, expand_script_name,
                                              match_info, error_r);
        if (script_name == nullptr)
            return false;
    }

    if (expand_path_info != nullptr) {
        path_info = expand_string_unescaped(pool, expand_path_info,
                                            match_info, error_r);
        if (path_info == nullptr)
            return false;
    }

    if (expand_document_root != nullptr) {
        document_root = expand_string_unescaped(pool, expand_document_root,
                                                match_info, error_r);
        if (document_root == nullptr)
            return false;
    }

    return args.Expand(pool, match_info, error_r) &&
        params.Expand(pool, match_info, error_r);
}
