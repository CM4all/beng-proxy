// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

/*
 * Utilities for buffered I/O.
 */

#pragma once

#include <cstddef>

#include <sys/types.h>

class FileDescriptor;
template<typename T> class ForeignFifoBuffer;

/**
 * Appends data from a file to the buffer.
 *
 * @param fd the source file descriptor
 * @param buffer the destination buffer
 * @return -1 on error, -2 if the buffer is full, or the amount appended to the buffer
 */
ssize_t
ReadToBuffer(FileDescriptor fd, ForeignFifoBuffer<std::byte> &buffer,
	     std::size_t length) noexcept;

ssize_t
ReadToBufferAt(FileDescriptor fd, off_t offset,
	       ForeignFifoBuffer<std::byte> &buffer,
	       std::size_t length) noexcept;
