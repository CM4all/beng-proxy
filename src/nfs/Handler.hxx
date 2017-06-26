/*
 * author: Max Kellermann <mk@cm4all.com>
 */

#ifndef BENG_PROXY_NFS_HANDLER_HXX
#define BENG_PROXY_NFS_HANDLER_HXX

#include "glibfwd.hxx"

#include <stddef.h>

struct stat;
class NfsClient;
class NfsFileHandle;

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

#endif
