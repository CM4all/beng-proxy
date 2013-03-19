/*
 * Glue for libnfs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_CLIENT_H
#define BENG_PROXY_NFS_CLIENT_H

#include <glib.h>

struct pool;
struct nfs_client;
struct http_response_handler;
struct async_operation_ref;

struct nfs_client_handler {
    /**
     * The export has been mounted successfully, and the #nfs_client
     * is now ready for I/O.
     */
    void (*ready)(struct nfs_client *client, void *ctx);

    /**
     * An error has occurred while trying to mount the export.
     */
    void (*mount_error)(GError *error, void *ctx);

    /**
     * The server has closed the connection.
     */
    void (*closed)(GError *error, void *ctx);
};

G_GNUC_CONST
static inline GQuark
nfs_client_quark(void)
{
    return g_quark_from_static_string("nfs_client");
}

void
nfs_client_new(struct pool *pool, const char *server, const char *root,
               const struct nfs_client_handler *handler, void *ctx,
               struct async_operation_ref *async_ref);

void
nfs_client_free(struct nfs_client *client);

void
nfs_client_get_file(struct nfs_client *client, struct pool *pool,
                    const char *path,
                    const struct http_response_handler *handler, void *ctx,
                    struct async_operation_ref *async_ref);

#endif
