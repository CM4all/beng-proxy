// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SpliceSupport.hxx"

#include <unistd.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>

#ifdef __linux

FdTypeMask ISTREAM_TO_PIPE = FdType::FD_FILE;
FdTypeMask ISTREAM_TO_CHARDEV = 0;

/**
 * Checks whether the kernel supports splice() between the two
 * specified file handle types.
 */
static bool
splice_supported(int src, int dest)
{
	return splice(src, NULL, dest, NULL, 1, SPLICE_F_NONBLOCK) >= 0 ||
		(errno != EINVAL && errno != ENOSYS);
}

void
direct_global_init()
{
	int a[2], b[2], fd;

	/* create a pipe and feed some data into it */

	if (pipe(a) < 0)
		abort();

	/* check splice(pipe, pipe) */

	if (pipe(b) < 0)
		abort();

	if (splice_supported(a[0], b[1]))
		ISTREAM_TO_PIPE |= FdType::FD_PIPE;

	close(b[0]);
	close(b[1]);

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

	/* check splice(AF_LOCAL, pipe) */

	if (socketpair(AF_LOCAL, SOCK_STREAM, 0, b) == 0) {
		if (splice_supported(b[0], a[1]))
			ISTREAM_TO_PIPE |= FdType::FD_SOCKET;

		close(b[0]);
		close(b[1]);
	}

	/* check splice(TCP, pipe) */

	fd = socket(AF_INET, SOCK_STREAM, 0);
	if (fd >= 0) {
		if (splice_supported(fd, a[1]))
			ISTREAM_TO_PIPE |= FdType::FD_TCP;

		close(fd);
	}

	/* cleanup */

	close(a[0]);
	close(a[1]);
}

#endif  /* #ifdef __linux */

FdType
guess_fd_type(int fd)
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
