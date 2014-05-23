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

    /**
     * Does this address need to be expanded with nfs_address_expand()?
     */
    gcc_pure
    bool IsExpandable() const {
        return expand_path != nullptr;
    }

    bool Check(GError **error_r) const;
};

struct nfs_address *
nfs_address_new(struct pool *pool, const char *server,
                const char *export_name, const char *path);

const char *
nfs_address_id(struct pool *pool, const struct nfs_address *address);

struct nfs_address *
nfs_address_dup(struct pool *pool, const struct nfs_address *src);

struct nfs_address *
nfs_address_save_base(struct pool *pool, const struct nfs_address *src,
                      const char *suffix);

struct nfs_address *
nfs_address_load_base(struct pool *pool, const struct nfs_address *src,
                      const char *suffix);

const struct nfs_address *
nfs_address_expand(struct pool *pool, const struct nfs_address *src,
                   const GMatchInfo *match_info, GError **error_r);

#endif
