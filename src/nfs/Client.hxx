/*
 * Glue for libnfs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_CLIENT_HXX
#define BENG_PROXY_NFS_CLIENT_HXX

#include <stdint.h>
#include <stddef.h>

struct pool;
class NfsClient;
class NfsClientHandler;
class NfsClientOpenFileHandler;
class NfsClientReadFileHandler;
class NfsFileHandle;
class HttpResponseHandler;
class CancellablePointer;
class EventLoop;

void
nfs_client_new(EventLoop &event_loop,
               struct pool &pool, const char *server, const char *root,
               NfsClientHandler &handler,
               CancellablePointer &cancel_ptr);

void
nfs_client_free(NfsClient *client);

void
nfs_client_open_file(NfsClient &client, struct pool &pool,
                     const char *path,
                     NfsClientOpenFileHandler &handler,
                     CancellablePointer &cancel_ptr);

void
nfs_client_close_file(NfsFileHandle &handle);

void
nfs_client_read_file(NfsFileHandle &handle,
                     uint64_t offset, size_t length,
                     NfsClientReadFileHandler &handler);

#endif
