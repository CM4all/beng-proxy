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

#ifdef HAVE_URING
#include "io/uring/Handler.hxx"
#include "io/uring/OpenStat.hxx"
#include "util/Cancellable.hxx"
#include <sys/sysmacros.h> // for makedev()
#endif

#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_URING

class UringStaticFileGet final : Uring::OpenStatHandler, Cancellable {
	struct pool &pool;
	EventLoop &event_loop;

	const char *const path;
	const char *const content_type;

	Uring::OpenStat open_stat;

	HttpResponseHandler &handler;

public:
	UringStaticFileGet(EventLoop &_event_loop, Uring::Queue &uring,
			   struct pool &_pool,
			   const char *_path,
			   const char *_content_type,
			   HttpResponseHandler &_handler) noexcept
		:pool(_pool), event_loop(_event_loop),
		 path(_path),
		 content_type(_content_type),
		 open_stat(uring, *this),
		 handler(_handler) {}

	void Start(CancellablePointer &cancel_ptr) noexcept {
		cancel_ptr = *this;
		open_stat.StartOpenStatReadOnly(path);
	}

private:
	void Destroy() noexcept {
		this->~UringStaticFileGet();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		Destroy();
	}

	/* virtual methods from class Uring::OpenStatHandler */
	void OnOpenStat(UniqueFileDescriptor fd,
			struct statx &st) noexcept override;

	void OnOpenStatError(std::exception_ptr e) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(std::move(e));
	}
};

void
UringStaticFileGet::OnOpenStat(UniqueFileDescriptor fd,
			       struct statx &stx) noexcept
{
	const char *_content_type = content_type;
	auto &_handler = handler;

	Destroy();

	if (S_ISCHR(stx.stx_mode)) {
		_handler.InvokeResponse(HTTP_STATUS_OK, {},
					NewFdIstream(event_loop, pool, path,
						     std::move(fd),
						     FdType::FD_CHARDEV));
		return;
	} else if (!S_ISREG(stx.stx_mode)) {
		_handler.InvokeResponse(pool, HTTP_STATUS_NOT_FOUND,
					"Not a regular file");
		return;
	}

	auto headers = static_response_headers(pool, fd, stx, _content_type);

	_handler.InvokeResponse(HTTP_STATUS_OK,
				std::move(headers),
				NewUringIstream(open_stat.GetQueue(), pool,
						path, std::move(fd),
						0, stx.stx_size));
}

#endif

void
static_file_get(EventLoop &event_loop,
#ifdef HAVE_URING
		Uring::Queue *uring,
#endif
		struct pool &pool,
		const char *path, const char *content_type,
		HttpResponseHandler &handler, CancellablePointer &cancel_ptr)
{
	assert(path != nullptr);

#ifdef HAVE_URING
	if (uring != nullptr) {
		auto *o = NewFromPool<UringStaticFileGet>(pool, event_loop,
							  *uring, pool,
							  path, content_type,
							  handler);
		o->Start(cancel_ptr);
		return;
	}
#else
	(void)cancel_ptr;
#endif

	UniqueFileDescriptor fd;
	struct statx st;

	try {
		fd = OpenReadOnly(path, O_NOFOLLOW);
		if (statx(fd.Get(), "", AT_EMPTY_PATH,
			  STATX_TYPE|STATX_MTIME|STATX_INO|STATX_SIZE,
			  &st) < 0)
			throw FormatErrno("Failed to stat %s", path);
	} catch (...) {
		handler.InvokeError(std::current_exception());
		return;
	}

	if (S_ISCHR(st.stx_mode)) {
		handler.InvokeResponse(HTTP_STATUS_OK, {},
				       NewFdIstream(event_loop, pool, path,
						    std::move(fd),
						    FdType::FD_CHARDEV));
		return;
	} else if (!S_ISREG(st.stx_mode)) {
		handler.InvokeResponse(pool, HTTP_STATUS_NOT_FOUND,
				       "Not a regular file");
		return;
	}

	auto headers = static_response_headers(pool, fd, st, content_type);

	handler.InvokeResponse(HTTP_STATUS_OK,
			       std::move(headers),
			       istream_file_fd_new(event_loop, pool, path,
						   std::move(fd),
						   0, st.stx_size));
}
