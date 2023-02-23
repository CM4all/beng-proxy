// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "CoStatAt.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "io/uring/CoOperation.hxx"
#include "co/Task.hxx"

#include <fcntl.h>

Co::Task<struct statx>
CoStatAt(Uring::Queue &queue,
	 const char *directory, const char *pathname, int flags,
	 unsigned mask) noexcept
{
	using namespace Uring;

	if (directory != nullptr) {
		const auto directory_fd = co_await CoOperation<CoOpenOperation>{
			queue, FileDescriptor{AT_FDCWD},
			directory,
			O_PATH, 0,
		};

		co_return co_await CoStatx{
			queue, directory_fd, pathname,
			flags, mask,
		};
	} else {
		co_return co_await CoStatx{
			queue, FileDescriptor{AT_FDCWD}, pathname,
			flags, mask,
		};
	}
}
