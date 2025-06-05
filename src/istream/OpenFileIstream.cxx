// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "OpenFileIstream.hxx"
#include "FileIstream.hxx"
#include "FdIstream.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/Open.hxx"
#include "io/SharedFd.hxx"
#include "io/UniqueFileDescriptor.hxx"

#include <utility>

#include <sys/stat.h>
#include <fcntl.h>

UnusedIstreamPtr
OpenFileIstream(EventLoop &event_loop, struct pool &pool, const char *path)
{
	auto fd = OpenReadOnly(path);

	struct stat st;
	if (fstat(fd.Get(), &st) < 0)
		throw FmtErrno("Failed to stat '{}'", path);

	if (!S_ISREG(st.st_mode)) {
		FdType fd_type = FdType::FD_NONE;
		if (S_ISCHR(st.st_mode))
			fd_type = FdType::FD_CHARDEV;
		else if (S_ISFIFO(st.st_mode))
			fd_type = FdType::FD_PIPE;
		else if (S_ISSOCK(st.st_mode))
			fd_type = FdType::FD_SOCKET;
		return NewFdIstream(event_loop, pool, path,
				    std::move(fd), fd_type);
	}

	auto *shared_fd = NewFromPool<SharedFd>(pool, std::move(fd));

	return istream_file_fd_new(event_loop, pool, path,
				   shared_fd->Get(), *shared_fd,
				   0, st.st_size);
}
