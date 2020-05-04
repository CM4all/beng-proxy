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

#include "file_request.hxx"
#include "static_headers.hxx"
#include "HttpResponseHandler.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/FileIstream.hxx"
#include "istream/FdIstream.hxx"
#include "istream/UringIstream.hxx"
#include "pool/pool.hxx"
#include "io/Open.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"
#include "http/Status.h"

#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>

void
static_file_get(EventLoop &event_loop,
#ifdef HAVE_URING
		Uring::Manager *uring,
#endif
		struct pool &pool,
		const char *path, const char *content_type,
		HttpResponseHandler &handler)
{
	assert(path != nullptr);

	UniqueFileDescriptor fd;
	struct stat st;

	try {
		fd = OpenReadOnly(path, O_NOFOLLOW);
		if (fstat(fd.Get(), &st) < 0)
			throw FormatErrno("Failed to stat %s", path);
	} catch (...) {
		handler.InvokeError(std::current_exception());
		return;
	}

	if (S_ISCHR(st.st_mode)) {
		handler.InvokeResponse(HTTP_STATUS_OK, {},
				       NewFdIstream(event_loop, pool, path,
						    std::move(fd),
						    FdType::FD_CHARDEV));
		return;
	} else if (!S_ISREG(st.st_mode)) {
		handler.InvokeResponse(pool, HTTP_STATUS_NOT_FOUND,
				       "Not a regular file");
		return;
	}

	auto headers = static_response_headers(pool, fd, st, content_type);

	handler.InvokeResponse(HTTP_STATUS_OK,
			       std::move(headers),
#ifdef HAVE_URING
			       uring != nullptr
			       ? NewUringIstream(*uring, pool, path,
						 std::move(fd), 0, st.st_size)
			       :
#endif
			       istream_file_fd_new(event_loop, pool, path,
						   std::move(fd),
						   0, st.st_size));
}
