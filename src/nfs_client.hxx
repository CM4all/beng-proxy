/*
 * Glue for libnfs.
 *
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_CLIENT_HXX
#define BENG_PROXY_NFS_CLIENT_HXX

#include <stdint.h>

#include <glib.h>

struct stat;
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

struct nfs_file_consumer {
    struct nfs_file_consumer_internal *internal;

    /**
     * The pool that was passed to nfs_client_open_file().  It will be
     * referenced by this library until nfs_client_close_file() is
     * called.
     */
    struct pool *pool;

    const struct nfs_file_consumer_handler *handler;
};

struct nfs_file_handle;

/**
 * Handler for nfs_client_open_file().
 */
struct nfs_client_open_file_handler {
    /**
     * The file has been opened and metadata is available.  The
     * consumer may now start I/O operations.
     */
    void (*ready)(struct nfs_file_handle *handle, const struct stat *st,
                  void *ctx);

    /**
     * An error has occurred while opening the file.
     */
    void (*error)(GError *error, void *ctx);
};

/**
 * Handler for nfs_client_read_file().
 */
struct nfs_client_read_file_handler {
    /**
     * Data has been read from the file.
     */
    void (*data)(const void *data, size_t length, void *ctx);

    /**
     * An I/O error has occurred while reading.
     */
    void (*error)(GError *error, void *ctx);
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
nfs_client_open_file(struct nfs_client *client, struct pool *pool,
                     const char *path,
                     const struct nfs_client_open_file_handler *handler,
                     void *ctx,
                     struct async_operation_ref *async_ref);

void
nfs_client_close_file(struct nfs_file_handle *handle);

void
nfs_client_read_file(struct nfs_file_handle *handle,
                     uint64_t offset, size_t length,
                     const struct nfs_client_read_file_handler *handler,
                     void *ctx);

#endif
