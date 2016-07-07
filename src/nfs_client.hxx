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
class HttpResponseHandler;
class CancellablePointer;
class EventLoop;

class NfsClientHandler {
public:
    /**
     * The export has been mounted successfully, and the #NfsClient
     * is now ready for I/O.
     */
    virtual void OnNfsClientReady(NfsClient &client) = 0;

    /**
     * An error has occurred while trying to mount the export.
     */
    virtual void OnNfsMountError(GError *error) = 0;

    /**
     * The server has closed the connection.
     */
    virtual void OnNfsClientClosed(GError *error) = 0;
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
nfs_client_new(EventLoop &event_loop,
               struct pool &pool, const char *server, const char *root,
               NfsClientHandler &handler,
               CancellablePointer &cancel_ptr);

void
nfs_client_free(NfsClient *client);

void
nfs_client_open_file(NfsClient *client, struct pool *pool,
                     const char *path,
                     const NfsClientOpenFileHandler *handler,
                     void *ctx,
                     CancellablePointer &cancel_ptr);

void
nfs_client_close_file(NfsFileHandle *handle);

void
nfs_client_read_file(NfsFileHandle *handle,
                     uint64_t offset, size_t length,
                     const NfsClientReadFileHandler *handler,
                     void *ctx);

#endif
