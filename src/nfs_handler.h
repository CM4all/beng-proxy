/*
 * Serve HTTP requests from a NFS server.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_HANDLER_H
#define BENG_PROXY_NFS_HANDLER_H

struct request;

#ifdef __cplusplus
extern "C" {
#endif

void
nfs_handler(struct request *request);

#ifdef __cplusplus
}
#endif

#endif
