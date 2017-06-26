/*
 * Serve HTTP requests from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_HANDLER_HXX
#define BENG_PROXY_NFS_HANDLER_HXX

struct Request;

void
nfs_handler(Request &request);

#endif
