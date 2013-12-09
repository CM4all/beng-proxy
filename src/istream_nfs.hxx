/*
 * istream implementation which reads a file from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_NFS_HXX
#define BENG_PROXY_ISTREAM_NFS_HXX

#include <stdint.h>

struct pool;
struct nfs_file_handle;

struct istream *
istream_nfs_new(struct pool *pool, struct nfs_file_handle *handle,
                uint64_t start, uint64_t end);

#endif
