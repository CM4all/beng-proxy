// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "OpenFileLease.hxx"
#include "pool/pool.hxx"
#include "io/Open.hxx"
#include "io/SharedFd.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"

#include <fcntl.h> // for AT_EMPTY_PATH
#include <sys/stat.h>

std::tuple<FileDescriptor, SharedLease, std::size_t>
OpenFileLease(struct pool &pool, const char *path)
{
	auto fd = OpenReadOnly(path);
	struct statx stx;
	if (statx(fd.Get(), "", AT_EMPTY_PATH, STATX_SIZE, &stx) < 0)
		throw MakeErrno("statx() failed");

	auto *shared_fd = NewFromPool<SharedFd>(pool, std::move(fd));
	return {
		shared_fd->Get(),
		*shared_fd,
		stx.stx_size,
	};
}
