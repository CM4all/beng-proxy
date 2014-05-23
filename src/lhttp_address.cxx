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
lhttp_address_server_id(struct pool *pool, const struct lhttp_address *address)
{
    char child_options_buffer[4096];
    *address->options.MakeId(child_options_buffer) = 0;

    const char *p = p_strcat(pool, address->path,
                             child_options_buffer,
                             NULL);

    for (unsigned i = 0; i < address->args.n; ++i)
        p = p_strcat(pool, p, "!", address->args.values[i], NULL);

    for (unsigned i = 0; i < address->env.n; ++i)
        p = p_strcat(pool, p, "$", address->env.values[i], nullptr);

    return p;
}

const char *
lhttp_address_id(struct pool *pool, const struct lhttp_address *address)
{
    const char *p = lhttp_address_server_id(pool, address);

    if (address->uri != NULL)
        p = p_strcat(pool, p, ";u=", address->uri, NULL);

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
lhttp_address_insert_query_string(struct pool *pool,
                                  const struct lhttp_address *src,
                                  const char *query_string)
{
    return lhttp_address_dup_with_uri(pool, src,
                                  uri_insert_query_string(pool, src->uri,
                                                          query_string));
}

struct lhttp_address *
lhttp_address_insert_args(struct pool *pool,
                          const struct lhttp_address *src,
                          const char *args, size_t args_length,
                          const char *path, size_t path_length)
{
    return lhttp_address_dup_with_uri(pool, src,
                                      uri_insert_args(pool, src->uri,
                                                      args, args_length,
                                                      path, path_length));
}

struct lhttp_address *
lhttp_address_save_base(struct pool *pool, const struct lhttp_address *src,
                        const char *suffix)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(suffix != NULL);

    size_t length = base_string(src->uri, suffix);
    if (length == (size_t)-1)
        return NULL;

    return lhttp_address_dup_with_uri(pool, src,
                                  p_strndup(pool, src->uri, length));
}

struct lhttp_address *
lhttp_address_load_base(struct pool *pool, const struct lhttp_address *src,
                        const char *suffix)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(suffix != NULL);
    assert(src->uri != NULL);
    assert(*src->uri != 0);
    assert(src->uri[strlen(src->uri) - 1] == '/');
    assert(suffix != NULL);

    return lhttp_address_dup_with_uri(pool, src,
                                      p_strcat(pool, src->uri, suffix, NULL));
}

const struct lhttp_address *
lhttp_address_apply(struct pool *pool, const struct lhttp_address *src,
                    const char *relative, size_t relative_length)
{
    if (relative_length == 0)
        return src;

    if (uri_has_protocol(relative, relative_length))
        return NULL;

    const char *p = uri_absolute(pool, src->path,
                                 relative, relative_length);
    assert(p != NULL);

    return lhttp_address_dup_with_uri(pool, src, p);
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
lhttp_address_expand(struct pool *pool, struct lhttp_address *address,
                     const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(address != NULL);
    assert(match_info != NULL);

    if (address->expand_uri != NULL) {
        address->uri = expand_string(pool, address->expand_uri,
                                     match_info, error_r);
        if (address->uri == NULL)
            return false;
    }

    return address->args.Expand(pool, match_info, error_r) &&
        address->env.Expand(pool, match_info, error_r);
}
