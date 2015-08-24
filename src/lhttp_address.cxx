/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_address.hxx"
#include "pool.hxx"
#include "uri_edit.hxx"
#include "uri_base.hxx"
#include "uri_relative.hxx"
#include "uri_escape.hxx"
#include "uri_extract.hxx"
#include "pexpand.hxx"
#include "strref.h"
#include "translate_quark.hxx"

#include <string.h>

void
lhttp_address_init(LhttpAddress *address, const char *path)
{
    assert(path != NULL);

    address->path = path;
    address->args.Init();
    address->env.Init();
    address->options.Init();
    address->host_and_port = nullptr;
    address->uri = address->expand_uri = nullptr;
    address->concurrency = 1;
    address->blocking = true;
}

LhttpAddress *
lhttp_address_new(struct pool &pool, const char *path)
{
    auto address = NewFromPool<LhttpAddress>(pool);
    lhttp_address_init(address, path);
    return address;
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

void
lhttp_address_copy(struct pool *pool, LhttpAddress *dest,
                   const LhttpAddress *src)
{
    assert(src->path != NULL);

    dest->path = p_strdup(pool, src->path);

    dest->args.CopyFrom(pool, src->args);
    dest->env.CopyFrom(pool, src->env);

    dest->options.CopyFrom(pool, &src->options);

    dest->host_and_port = p_strdup_checked(pool, src->host_and_port);
    dest->uri = p_strdup(pool, src->uri);
    dest->expand_uri = p_strdup_checked(pool, src->expand_uri);

    dest->concurrency = src->concurrency;
    dest->blocking = src->blocking;
}

LhttpAddress *
lhttp_address_dup(struct pool &pool, const LhttpAddress *old)
{
    auto n = NewFromPool<LhttpAddress>(pool);
    lhttp_address_copy(&pool, n, old);
    return n;
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
lhttp_address_dup_with_uri(struct pool &pool, const LhttpAddress *src,
                           const char *uri)
{
    LhttpAddress *p = lhttp_address_dup(pool, src);
    p->uri = uri;
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
    return lhttp_address_dup_with_uri(pool, this,
                                      uri_insert_query_string(&pool, uri,
                                                              query_string));
}

LhttpAddress *
LhttpAddress::InsertArgs(struct pool &pool,
                          const char *new_args, size_t new_args_length,
                          const char *path_info, size_t path_info_length) const
{
    return lhttp_address_dup_with_uri(pool, this,
                                      uri_insert_args(&pool, uri,
                                                      new_args,
                                                      new_args_length,
                                                      path_info,
                                                      path_info_length));
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

    return lhttp_address_dup_with_uri(*pool, this,
                                      p_strndup(pool, uri, length));
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

    return lhttp_address_dup_with_uri(*pool, this,
                                      p_strcat(pool, uri, suffix, nullptr));
}

const LhttpAddress *
LhttpAddress::Apply(struct pool *pool, const char *relative,
                     size_t relative_length) const
{
    if (relative_length == 0)
        return this;

    if (uri_has_protocol(relative, relative_length))
        return nullptr;

    const char *p = uri_absolute(pool, path,
                                 relative, relative_length);
    assert(p != nullptr);

    return lhttp_address_dup_with_uri(*pool, this, p);
}

const struct strref *
lhttp_address_relative(const LhttpAddress *base,
                       const LhttpAddress *address,
                       struct strref *buffer)
{
    if (strcmp(base->path, address->path) != 0)
        return NULL;

    struct strref base_uri;
    strref_set_c(&base_uri, base->uri);
    strref_set_c(buffer, address->uri);
    return uri_relative(&base_uri, buffer);
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
