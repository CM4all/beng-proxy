/*
 * istream implementation which reads a file from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_ISTREAM_NFS_HXX
#define BENG_PROXY_ISTREAM_NFS_HXX

#include <stdint.h>

struct pool;
class Istream;
struct NfsFileHandle;

Istream *
istream_nfs_new(struct pool &pool, NfsFileHandle &handle,
                uint64_t start, uint64_t end);

#endif
