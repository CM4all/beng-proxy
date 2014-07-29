/*
 * Serve HTTP requests from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_HANDLER_HXX
#define BENG_PROXY_NFS_HANDLER_HXX

struct request;

void
nfs_handler(struct request &request);

#endif
