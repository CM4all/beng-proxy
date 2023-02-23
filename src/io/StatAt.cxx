// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "StatAt.hxx"
#include "io/UniqueFileDescriptor.hxx"

#include <fcntl.h>
#include <sys/stat.h>

bool
StatAt(const char *directory, const char *pathname, int flags,
       unsigned mask, struct statx *statxbuf) noexcept
{
	if (directory != nullptr) {
		UniqueFileDescriptor directory_fd;
		return directory_fd.Open(directory, O_PATH) &&
			statx(directory_fd.Get(), pathname,
			      flags, mask, statxbuf) == 0;
	} else
		return statx(AT_FDCWD, pathname,
			     flags, mask, statxbuf) == 0;
}
