/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_ADDRESS_HXX
#define BENG_PROXY_NFS_ADDRESS_HXX

#include "glibfwd.hxx"

#include <inline/compiler.h>

struct pool;

/**
 * The address of a file on a NFS server.
 */
struct nfs_address {
    const char *server;

    const char *export_name;

    const char *path;

    /**
     * The value of #TRANSLATE_EXPAND_PATH.  Only used by the
     * translation cache.
     */
    const char *expand_path;

    const char *content_type;

    nfs_address(const char *_server,
                const char *_export_name, const char *_path)
        :server(_server), export_name(_export_name), path(_path),
         expand_path(nullptr), content_type(nullptr) {}

    nfs_address(struct pool *pool, const nfs_address &other);

    const char *GetId(struct pool *pool) const;

    bool Check(GError **error_r) const;

    gcc_pure
    bool IsValidBase() const;

    struct nfs_address *SaveBase(struct pool *pool, const char *suffix) const;

    struct nfs_address *LoadBase(struct pool *pool, const char *suffix) const;

    /**
     * Does this address need to be expanded with nfs_address_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return expand_path != nullptr;
    }

    const struct nfs_address *Expand(struct pool *pool,
                                     const GMatchInfo *match_info,
                                     GError **error_r) const;
};

struct nfs_address *
nfs_address_new(struct pool *pool, const char *server,
                const char *export_name, const char *path);

struct nfs_address *
nfs_address_dup(struct pool *pool, const struct nfs_address *src);

#endif
