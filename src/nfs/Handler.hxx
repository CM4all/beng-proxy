// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#pragma once

#include <exception>

#include <stddef.h>

struct statx;
class NfsClient;
class NfsFileHandle;

class NfsClientHandler {
public:
	/**
	 * The export has been mounted successfully, and the #NfsClient
	 * is now ready for I/O.
	 */
	virtual void OnNfsClientReady(NfsClient &client) noexcept = 0;

	/**
	 * An error has occurred while trying to mount the export.
	 */
	virtual void OnNfsMountError(std::exception_ptr ep) noexcept = 0;

	/**
	 * The server has closed the connection.
	 */
	virtual void OnNfsClientClosed(std::exception_ptr ep) noexcept = 0;
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
	virtual void OnNfsOpen(NfsFileHandle *handle,
			       const struct statx &st) noexcept = 0;

	/**
	 * An error has occurred while opening the file.
	 */
	virtual void OnNfsOpenError(std::exception_ptr ep) noexcept = 0;
};

/**
 * Handler for nfs_client_read_file().
 */
class NfsClientReadFileHandler {
public:
	/**
	 * Data has been read from the file.
	 */
	virtual void OnNfsRead(const void *data, size_t length) noexcept = 0;

	/**
	 * An I/O error has occurred while reading.
	 */
	virtual void OnNfsReadError(std::exception_ptr ep) noexcept = 0;
};
