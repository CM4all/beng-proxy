/*
 * A cache for NFS files.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_CACHE_H
#define BENG_PROXY_NFS_CACHE_H

#include <glib.h>

#include <stddef.h>
#include <stdint.h>

struct pool;
struct nfs_cache;
struct nfs_cache_item;
struct nfs_stock;
struct nfs_cache_handle;
struct async_operation_ref;
struct rubber;
struct stat;

struct nfs_cache_handler {
    void (*response)(struct nfs_cache_handle *handle,
                     const struct stat *st, void *ctx);
    void (*error)(GError *error, void *ctx);
};

struct nfs_cache *
nfs_cache_new(struct pool *pool, size_t max_size, struct nfs_stock *stock);

void
nfs_cache_free(struct nfs_cache *cache);

void
nfs_cache_request(struct pool *pool, struct nfs_cache *cache,
                  const char *server, const char *export, const char *path,
                  const struct nfs_cache_handler *handler, void *ctx,
                  struct async_operation_ref *async_ref);

struct istream *
nfs_cache_handle_open(struct pool *pool, struct nfs_cache_handle *handle,
                      uint64_t start, uint64_t end);

#endif
