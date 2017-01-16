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
class NfsClient;
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

class NfsFileHandle;

/**
 * Handler for nfs_client_open_file().
 */
class NfsClientOpenFileHandler {
public:
    /**
     * The file has been opened and metadata is available.  The
     * consumer may now start I/O operations.
     */
    virtual void OnNfsOpen(NfsFileHandle *handle, const struct stat *st) = 0;

    /**
     * An error has occurred while opening the file.
     */
    virtual void OnNfsOpenError(GError *error) = 0;
};

/**
 * Handler for nfs_client_read_file().
 */
class NfsClientReadFileHandler {
public:
    /**
     * Data has been read from the file.
     */
    virtual void OnNfsRead(const void *data, size_t length) = 0;

    /**
     * An I/O error has occurred while reading.
     */
    virtual void OnNfsReadError(GError *error) = 0;
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
                     NfsClientOpenFileHandler &handler,
                     CancellablePointer &cancel_ptr);

void
nfs_client_close_file(NfsFileHandle *handle);

void
nfs_client_read_file(NfsFileHandle *handle,
                     uint64_t offset, size_t length,
                     NfsClientReadFileHandler &handler);

#endif
