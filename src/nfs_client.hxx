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
struct NfsClient;
struct http_response_handler;
struct async_operation_ref;

struct NfsClientHandler {
    /**
     * The export has been mounted successfully, and the #NfsClient
     * is now ready for I/O.
     */
    void (*ready)(NfsClient *client, void *ctx);

    /**
     * An error has occurred while trying to mount the export.
     */
    void (*mount_error)(GError *error, void *ctx);

    /**
     * The server has closed the connection.
     */
    void (*closed)(GError *error, void *ctx);
};

struct NfsFileHandle;

/**
 * Handler for nfs_client_open_file().
 */
struct NfsClientOpenFileHandler {
    /**
     * The file has been opened and metadata is available.  The
     * consumer may now start I/O operations.
     */
    void (*ready)(NfsFileHandle *handle, const struct stat *st,
                  void *ctx);

    /**
     * An error has occurred while opening the file.
     */
    void (*error)(GError *error, void *ctx);
};

/**
 * Handler for nfs_client_read_file().
 */
struct NfsClientReadFileHandler {
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
               const NfsClientHandler *handler, void *ctx,
               struct async_operation_ref *async_ref);

void
nfs_client_free(NfsClient *client);

void
nfs_client_open_file(NfsClient *client, struct pool *pool,
                     const char *path,
                     const NfsClientOpenFileHandler *handler,
                     void *ctx,
                     struct async_operation_ref *async_ref);

void
nfs_client_close_file(NfsFileHandle *handle);

void
nfs_client_read_file(NfsFileHandle *handle,
                     uint64_t offset, size_t length,
                     const NfsClientReadFileHandler *handler,
                     void *ctx);

#endif
