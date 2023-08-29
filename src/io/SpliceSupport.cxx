// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SpliceSupport.hxx"

#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

FdTypeMask ISTREAM_TO_PIPE = FdType::FD_FILE | FdType::FD_PIPE | FdType::FD_SOCKET | FdType::FD_TCP;
FdTypeMask ISTREAM_TO_CHARDEV = 0;

/**
 * Checks whether the kernel supports splice() between the two
 * specified file handle types.
 */
[[gnu::pure]]
static bool
splice_supported(int src, int dest) noexcept
{
	return splice(src, NULL, dest, NULL, 1, SPLICE_F_NONBLOCK) >= 0 ||
		(errno != EINVAL && errno != ENOSYS);
}

void
direct_global_init() noexcept
{
	int a[2], fd;

	/* create a pipe and feed some data into it */

	if (pipe(a) < 0)
		abort();

	/* check splice(pipe, chardev) */

	fd = open("/dev/null", O_WRONLY);
	if (fd >= 0) {
		if (splice_supported(a[0], fd))
			ISTREAM_TO_CHARDEV |= FdType::FD_PIPE;
		close(fd);
	}

	/* check splice(chardev, pipe) */

	fd = open("/dev/zero", O_RDONLY);
	if (fd >= 0) {
		if (splice_supported(fd, a[1]))
			ISTREAM_TO_PIPE |= FdType::FD_CHARDEV;
		close(fd);
	}

	/* cleanup */

	close(a[0]);
	close(a[1]);
}

FdType
guess_fd_type(int fd) noexcept
{
	struct statx stx;
	if (statx(fd, "", AT_EMPTY_PATH|AT_NO_AUTOMOUNT|AT_SYMLINK_NOFOLLOW|AT_STATX_DONT_SYNC,
		  STATX_TYPE, &stx) < 0)
		return FdType::FD_NONE;

	if (S_ISREG(stx.stx_mode))
		return FdType::FD_FILE;

	if (S_ISCHR(stx.stx_mode))
		return FdType::FD_CHARDEV;

	if (S_ISFIFO(stx.stx_mode))
		return FdType::FD_PIPE;

	if (S_ISSOCK(stx.stx_mode))
		return FdType::FD_SOCKET;

	return FdType::FD_NONE;
}
