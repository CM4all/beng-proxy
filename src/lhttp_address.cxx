/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_address.hxx"
#include "pool.hxx"
#include "uri/uri_base.hxx"
#include "uri/uri_relative.hxx"
#include "uri/uri_escape.hxx"
#include "uri/uri_extract.hxx"
#include "puri_edit.hxx"
#include "puri_relative.hxx"
#include "pexpand.hxx"
#include "strref.h"
#include "translate_quark.hxx"
#include "util/StringView.hxx"

#include <string.h>

LhttpAddress::LhttpAddress(const char *_path)
    :path(_path),
     host_and_port(nullptr),
     uri(nullptr), expand_uri(nullptr),
     concurrency(1),
     blocking(true)
{
    assert(path != nullptr);

    args.Init();
    env.Init();
    options.Init();
}

LhttpAddress::LhttpAddress(struct pool &pool, const LhttpAddress &src)
    :path(p_strdup(&pool, src.path)),
     host_and_port(p_strdup_checked(&pool, src.host_and_port)),
     uri(p_strdup(&pool, src.uri)),
     expand_uri(p_strdup_checked(&pool, src.expand_uri)),
     concurrency(src.concurrency),
     blocking(src.blocking)
{
    assert(src.path != nullptr);

    args.CopyFrom(&pool, src.args);
    env.CopyFrom(&pool, src.env);
    options.CopyFrom(&pool, &src.options);
}

const char *
LhttpAddress::GetServerId(struct pool *pool) const
{
    char child_options_buffer[4096];
    *options.MakeId(child_options_buffer) = 0;

    const char *p = p_strcat(pool, path,
                             child_options_buffer,
                             nullptr);

    for (auto i : args)
        p = p_strcat(pool, p, "!", i, nullptr);

    for (auto i : env)
        p = p_strcat(pool, p, "$", i, nullptr);

    return p;
}

const char *
LhttpAddress::GetId(struct pool *pool) const
{
    const char *p = GetServerId(pool);

    if (uri != nullptr)
        p = p_strcat(pool, p, ";u=", uri, nullptr);

    return p;
}

LhttpAddress *
LhttpAddress::Dup(struct pool &pool) const
{
    return NewFromPool<LhttpAddress>(pool, pool, *this);
}

bool
LhttpAddress::Check(GError **error_r) const
{
    if (uri == nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "missing LHTTP_URI");
        return false;
    }

    return options.Check(error_r);
}

LhttpAddress *
LhttpAddress::DupWithUri(struct pool &pool, const char *new_uri) const
{
    LhttpAddress *p = Dup(pool);
    p->uri = new_uri;
    return p;
}

bool
LhttpAddress::HasQueryString() const
{
    return strchr(uri, '?') != nullptr;
}

LhttpAddress *
LhttpAddress::InsertQueryString(struct pool &pool,
                                const char *query_string) const
{
    return DupWithUri(pool, uri_insert_query_string(&pool, uri, query_string));
}

LhttpAddress *
LhttpAddress::InsertArgs(struct pool &pool,
                          const char *new_args, size_t new_args_length,
                          const char *path_info, size_t path_info_length) const
{
    return DupWithUri(pool,
                      uri_insert_args(&pool, uri,
                                      new_args, new_args_length,
                                      path_info, path_info_length));
}

bool
LhttpAddress::IsValidBase() const
{
    return IsExpandable() || is_base(uri);
}

LhttpAddress *
LhttpAddress::SaveBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    size_t length = base_string(uri, suffix);
    if (length == (size_t)-1)
        return nullptr;

    return DupWithUri(*pool, p_strndup(pool, uri, length));
}

LhttpAddress *
LhttpAddress::LoadBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);
    assert(uri != nullptr);
    assert(*uri != 0);
    assert(uri[strlen(uri) - 1] == '/');
    assert(suffix != nullptr);

    return DupWithUri(*pool, p_strcat(pool, uri, suffix, nullptr));
}

const LhttpAddress *
LhttpAddress::Apply(struct pool *pool, const char *relative,
                     size_t relative_length) const
{
    if (relative_length == 0)
        return this;

    if (uri_has_authority({relative, relative_length}))
        return nullptr;

    const char *p = uri_absolute(pool, path,
                                 relative, relative_length);
    assert(p != nullptr);

    return DupWithUri(*pool, p);
}

const struct strref *
LhttpAddress::RelativeTo(const LhttpAddress &base,
                         struct strref &buffer) const
{
    if (strcmp(base.path, path) != 0)
        return nullptr;

    struct strref base_uri;
    strref_set_c(&base_uri, base.uri);
    strref_set_c(&buffer, uri);
    return uri_relative(&base_uri, &buffer);
}

bool
LhttpAddress::Expand(struct pool *pool, const MatchInfo &match_info,
                      Error &error_r)
{
    assert(pool != NULL);

    if (!options.Expand(*pool, match_info, error_r))
        return false;

    if (expand_uri != NULL) {
        uri = expand_string(pool, expand_uri, match_info, error_r);
        if (uri == NULL)
            return false;
    }

    return args.Expand(pool, match_info, error_r) &&
        env.Expand(pool, match_info, error_r);
}
