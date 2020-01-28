/*
 * Copyright 2007-2020 CM4all GmbH
 * All rights reserved.
 *
 * author: Max Kellermann <mk@cm4all.com>
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 * notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 * notice, this list of conditions and the following disclaimer in the
 * documentation and/or other materials provided with the
 * distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * ``AS IS'' AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN NO EVENT SHALL THE
 * FOUNDATION OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

#pragma once

#include <exception>

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
			       const struct stat *st) noexcept = 0;

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
