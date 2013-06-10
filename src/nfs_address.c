/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#include "nfs_address.h"
#include "uri-base.h"
#include "uri-escape.h"
#include "regex.h"
#include "pool.h"

#include <assert.h>
#include <string.h>

struct nfs_address *
nfs_address_new(struct pool *pool, const char *server,
                const char *export, const char *path)
{
    struct nfs_address *nfs = p_malloc(pool, sizeof(*nfs));
    nfs->server = p_strdup(pool, server);
    nfs->export = p_strdup(pool, export);
    nfs->path = p_strdup(pool, path);
    nfs->expand_path = NULL;
    nfs->content_type = NULL;
    return nfs;
}

const char *
nfs_address_id(struct pool *pool, const struct nfs_address *address)
{
    assert(address != NULL);
    assert(address->server != NULL);
    assert(address->export != NULL);
    assert(address->path != NULL);

    return p_strcat(pool, address->server, ":", address->export, ":",
                    address->path, NULL);
}

void
nfs_address_copy(struct pool *pool, struct nfs_address *dest,
                 const struct nfs_address *src)
{
    assert(dest != NULL);
    assert(src != NULL);
    assert(src->server != NULL);
    assert(src->export != NULL);
    assert(src->path != NULL);

    dest->server = p_strdup(pool, src->server);
    dest->export = p_strdup(pool, src->export);
    dest->path = p_strdup(pool, src->path);
    dest->expand_path = p_strdup_checked(pool, src->expand_path);
    dest->content_type = p_strdup_checked(pool, src->content_type);
}

struct nfs_address *
nfs_address_dup(struct pool *pool, const struct nfs_address *src)
{
    struct nfs_address *dest = p_malloc(pool, sizeof(*dest));
    nfs_address_copy(pool, dest, src);
    return dest;
}

struct nfs_address *
nfs_address_save_base(struct pool *pool, const struct nfs_address *src,
                      const char *suffix)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(suffix != NULL);

    size_t length = base_string_unescape(pool, src->path, suffix);
    if (length == (size_t)-1)
        return NULL;

    struct nfs_address *dest = p_malloc(pool, sizeof(*dest));
    dest->server = p_strdup(pool, src->server);
    dest->export = p_strdup(pool, src->export);
    dest->path = p_strndup(pool, dest->path, length);
    dest->expand_path = NULL;
    dest->content_type = p_strdup_checked(pool, src->content_type);
    return dest;
}

struct nfs_address *
nfs_address_load_base(struct pool *pool, const struct nfs_address *src,
                      const char *suffix)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(src->path != NULL);
    assert(*src->path != 0);
    assert(src->path[strlen(src->path) - 1] == '/');
    assert(suffix != NULL);

    char *unescaped = p_strdup(pool, suffix);
    unescaped[uri_unescape_inplace(unescaped, strlen(unescaped), '%')] = 0;

    struct nfs_address *dest = p_malloc(pool, sizeof(*dest));
    dest->server = p_strdup(pool, src->server);
    dest->export = p_strdup(pool, src->export);
    dest->path = p_strcat(pool, dest->path, unescaped, NULL);
    dest->expand_path = NULL;
    dest->content_type = p_strdup_checked(pool, src->content_type);
    return dest;
}

const struct nfs_address *
nfs_address_expand(struct pool *pool, const struct nfs_address *src,
                   const GMatchInfo *match_info, GError **error_r)
{
    assert(pool != NULL);
    assert(src != NULL);
    assert(match_info != NULL);

    if (src->expand_path == NULL)
        return src;

    const char *path = expand_string(pool, src->expand_path, match_info,
                                     error_r);
    if (path == NULL)
        return NULL;

    struct nfs_address *dest = p_malloc(pool, sizeof(*dest));
    dest->server = src->server;
    dest->export = src->export;
    dest->path = path;
    dest->expand_path = NULL;
    dest->content_type = p_strdup_checked(pool, src->content_type);
    return dest;
}
