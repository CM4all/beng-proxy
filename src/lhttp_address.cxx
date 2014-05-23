/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "lhttp_address.hxx"
#include "pool.h"
#include "uri-edit.h"
#include "uri_base.hxx"
#include "uri-relative.h"
#include "uri_escape.hxx"
#include "uri-extract.h"
#include "regex.hxx"
#include "strref.h"
#include "translate_client.hxx"

#include <string.h>

void
lhttp_address_init(struct lhttp_address *address, const char *path)
{
    assert(path != NULL);

    memset(address, 0, sizeof(*address));
    address->path = path;
    address->args.Init();
    address->env.Init();
    address->options.Init();
    address->concurrency = 1;
}

struct lhttp_address *
lhttp_address_new(struct pool *pool, const char *path)
{
    auto address = NewFromPool<struct lhttp_address>(pool);
    lhttp_address_init(address, path);
    return address;
}

const char *
lhttp_address::GetServerId(struct pool *pool) const
{
    char child_options_buffer[4096];
    *options.MakeId(child_options_buffer) = 0;

    const char *p = p_strcat(pool, path,
                             child_options_buffer,
                             nullptr);

    for (unsigned i = 0; i < args.n; ++i)
        p = p_strcat(pool, p, "!", args.values[i], nullptr);

    for (unsigned i = 0; i < env.n; ++i)
        p = p_strcat(pool, p, "$", env.values[i], nullptr);

    return p;
}

const char *
lhttp_address::GetId(struct pool *pool) const
{
    const char *p = GetServerId(pool);

    if (uri != nullptr)
        p = p_strcat(pool, p, ";u=", uri, nullptr);

    return p;
}

void
lhttp_address_copy(struct pool *pool, struct lhttp_address *dest,
                   const struct lhttp_address *src)
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
}

struct lhttp_address *
lhttp_address_dup(struct pool *pool, const struct lhttp_address *old)
{
    auto n = NewFromPool<struct lhttp_address>(pool);
    lhttp_address_copy(pool, n, old);
    return n;
}

bool
lhttp_address::Check(GError **error_r) const
{
    if (uri == nullptr) {
        g_set_error_literal(error_r, translate_quark(), 0,
                            "missing LHTTP_URI");
        return false;
    }

    return true;
}

struct lhttp_address *
lhttp_address_dup_with_uri(struct pool *pool, const struct lhttp_address *src,
                           const char *uri)
{
    struct lhttp_address *p = lhttp_address_dup(pool, src);
    p->uri = uri;
    return p;
}

struct lhttp_address *
lhttp_address::InsertQueryString(struct pool *pool,
                                 const char *query_string) const
{
    return lhttp_address_dup_with_uri(pool, this,
                                      uri_insert_query_string(pool, uri,
                                                              query_string));
}

struct lhttp_address *
lhttp_address::InsertArgs(struct pool *pool,
                          const char *new_args, size_t new_args_length,
                          const char *path_info, size_t path_info_length) const
{
    return lhttp_address_dup_with_uri(pool, this,
                                      uri_insert_args(pool, uri,
                                                      new_args,
                                                      new_args_length,
                                                      path_info,
                                                      path_info_length));
}

bool
lhttp_address::IsValidBase() const
{
    return IsExpandable() || is_base(uri);
}

struct lhttp_address *
lhttp_address::SaveBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);

    size_t length = base_string(uri, suffix);
    if (length == (size_t)-1)
        return nullptr;

    return lhttp_address_dup_with_uri(pool, this,
                                      p_strndup(pool, uri, length));
}

struct lhttp_address *
lhttp_address::LoadBase(struct pool *pool, const char *suffix) const
{
    assert(pool != nullptr);
    assert(suffix != nullptr);
    assert(uri != nullptr);
    assert(*uri != 0);
    assert(uri[strlen(uri) - 1] == '/');
    assert(suffix != nullptr);

    return lhttp_address_dup_with_uri(pool, this,
                                      p_strcat(pool, uri, suffix, nullptr));
}

const struct lhttp_address *
lhttp_address::Apply(struct pool *pool, const char *relative,
                     size_t relative_length) const
{
    if (relative_length == 0)
        return this;

    if (uri_has_protocol(relative, relative_length))
        return nullptr;

    const char *p = uri_absolute(pool, path,
                                 relative, relative_length);
    assert(p != nullptr);

    return lhttp_address_dup_with_uri(pool, this, p);
}

const struct strref *
lhttp_address_relative(const struct lhttp_address *base,
                       const struct lhttp_address *address,
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
lhttp_address::Expand(struct pool *pool, const GMatchInfo *match_info,
                      GError **error_r)
{
    assert(pool != NULL);
    assert(match_info != NULL);

    if (expand_uri != NULL) {
        uri = expand_string(pool, expand_uri, match_info, error_r);
        if (uri == NULL)
            return false;
    }

    return args.Expand(pool, match_info, error_r) &&
        env.Expand(pool, match_info, error_r);
}
