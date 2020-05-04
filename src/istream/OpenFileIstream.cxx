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

#include "OpenFileIstream.hxx"
#include "FileIstream.hxx"
#include "FdIstream.hxx"
#include "UnusedPtr.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"

#include <utility>

#include <sys/stat.h>
#include <fcntl.h>

UnusedIstreamPtr
OpenFileIstream(EventLoop &event_loop, struct pool &pool, const char *path)
{
	auto fd = OpenReadOnly(path);

	struct stat st;
	if (fstat(fd.Get(), &st) < 0)
		throw FormatErrno("Failed to stat %s", path);

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

	return istream_file_fd_new(event_loop, pool, path,
				   std::move(fd), 0, st.st_size);
}
