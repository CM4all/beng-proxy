/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_ADDRESS_H
#define BENG_PROXY_NFS_ADDRESS_H

#include <inline/compiler.h>

#include <glib.h>
#include <stdbool.h>
#include <stddef.h>

struct pool;

/**
 * The address of a file on a NFS server.
 */
struct nfs_address {
    const char *server;

    const char *export;

    const char *path;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path;
};

static inline void
nfs_address_init(struct nfs_address *nfs, const char *server,
                 const char *export, const char *path)
{
    nfs->server = server;
    nfs->export = export;
    nfs->path = path;
    nfs->expand_path = NULL;
}

struct nfs_address *
nfs_address_new(struct pool *pool, const char *server,
                const char *export, const char *path);

const char *
nfs_address_id(struct pool *pool, const struct nfs_address *address);

void
nfs_address_copy(struct pool *pool, struct nfs_address *dest,
                 const struct nfs_address *src);

struct nfs_address *
nfs_address_dup(struct pool *pool, const struct nfs_address *src);

struct nfs_address *
nfs_address_save_base(struct pool *pool, const struct nfs_address *src,
                      const char *suffix);

struct nfs_address *
nfs_address_load_base(struct pool *pool, const struct nfs_address *src,
                      const char *suffix);

/**
 * Does this address need to be expanded with nfs_address_expand()?
 */
gcc_pure
static inline bool
nfs_address_is_expandable(const struct nfs_address *address)
{
    return address->expand_path != NULL;
}

const struct nfs_address *
nfs_address_expand(struct pool *pool, const struct nfs_address *src,
                   const GMatchInfo *match_info, GError **error_r);

#endif
