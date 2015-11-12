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
#include "pexpand.hxx"

#include <string.h>

void
cgi_address_init(struct cgi_address *cgi, const char *path,
                 bool have_address_list)
{
    assert(path != nullptr);

    memset(cgi, 0, sizeof(*cgi));
    cgi->path = path;

    cgi->args.Init();
    cgi->params.Init();
    cgi->options.Init();

    if (have_address_list)
        cgi->address_list.Init();
}

struct cgi_address *
cgi_address_new(struct pool &pool, const char *path, bool have_address_list)
{
    auto cgi = NewFromPool<struct cgi_address>(pool);
    cgi_address_init(cgi, path, have_address_list);
    return cgi;
}

gcc_pure
static bool
HasTrailingSlash(const char *p)
{
    size_t length = strlen(p);
    return length > 0 && p[length - 1] == '/';
}

const char *
cgi_address::GetURI(struct pool *pool) const
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
cgi_address::GetId(struct pool *pool) const
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

void
cgi_address::CopyFrom(struct pool &p, const struct cgi_address &src,
                      bool have_address_list)
{
    assert(src.path != nullptr);

    path = p_strdup(&p, src.path);

    args.CopyFrom(&p, src.args);
    params.CopyFrom(&p, src.params);

    options.CopyFrom(&p, &src.options);

    interpreter = p_strdup_checked(&p, src.interpreter);
    action = p_strdup_checked(&p, src.action);
    uri = p_strdup_checked(&p, src.uri);
    expand_uri = p_strdup_checked(&p, src.expand_uri);
    script_name = p_strdup_checked(&p, src.script_name);
    expand_script_name = p_strdup_checked(&p, src.expand_script_name);
    path_info = p_strdup_checked(&p, src.path_info);
    expand_path = p_strdup_checked(&p, src.expand_path);
    expand_path_info = p_strdup_checked(&p, src.expand_path_info);
    expand_document_root = p_strdup_checked(&p, src.expand_document_root);
    query_string = p_strdup_checked(&p, src.query_string);
    document_root = p_strdup_checked(&p, src.document_root);

    if (have_address_list)
        address_list.CopyFrom(&p, src.address_list);
}

struct cgi_address *
cgi_address::Clone(struct pool &p, bool have_address_list) const
{
    auto n = NewFromPool<struct cgi_address>(p);
    n->CopyFrom(p, *this, have_address_list);
    return n;
}

bool
cgi_address::IsValidBase() const
{
    return IsExpandable() || (path_info != nullptr && is_base(path_info));
}

char *
cgi_address::AutoBase(struct pool *pool, const char *request_uri) const
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

struct cgi_address *
cgi_address::SaveBase(struct pool *pool, const char *suffix,
                      bool have_address_list) const
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

    struct cgi_address *dest = Clone(*pool, have_address_list);
    if (dest->uri != nullptr)
        dest->uri = p_strndup(pool, dest->uri, uri_length);
    dest->path_info = p_strndup(pool, new_path_info, length);
    return dest;
}

struct cgi_address *
cgi_address::LoadBase(struct pool *pool, const char *suffix,
                      bool have_address_list) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    char *unescaped = uri_unescape_dup(pool, suffix);
    if (unescaped == nullptr)
        return nullptr;

    struct cgi_address *dest = Clone(*pool, have_address_list);
    if (dest->uri != nullptr)
        dest->uri = p_strcat(pool, dest->uri, unescaped, nullptr);

    const char *new_path_info = path_info != nullptr ? path_info : "";
    dest->path_info = p_strcat(pool, new_path_info, unescaped, nullptr);
    return dest;
}

const struct cgi_address *
cgi_address::Apply(struct pool *pool,
                   const char *relative, size_t relative_length,
                   bool have_address_list) const
{
    if (relative_length == 0)
        return this;

    if (uri_has_authority({relative, relative_length}))
        return nullptr;

    char *unescaped = (char *)p_malloc(pool, relative_length);
    char *unescaped_end = uri_unescape(unescaped, {relative, relative_length});
    if (unescaped_end == nullptr)
        return nullptr;

    size_t unescaped_length = unescaped_end - unescaped;

    const char *new_path_info = path_info != nullptr ? path_info : "";

    struct cgi_address *dest = Clone(*pool, have_address_list);
    dest->path_info = uri_absolute(pool, new_path_info,
                                   unescaped, unescaped_length);
    assert(dest->path_info != nullptr);
    return dest;
}

bool
cgi_address::Expand(struct pool *pool, const MatchInfo &match_info,
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
