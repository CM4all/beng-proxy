// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "HttpRequest.hxx"
#include "Handler.hxx"
#include "Glue.hxx"
#include "file/Headers.hxx"
#include "http/ResponseHandler.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/FileIstream.hxx"
#include "pool/pool.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/SharedFd.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "AllocatorPtr.hxx"

#include <fcntl.h>
#include <sys/stat.h>

class DelegateHttpRequest final : DelegateHandler {
	EventLoop &event_loop;
	struct pool &pool;
	const char *const path;
	const char *const content_type;
	HttpResponseHandler &handler;
	const bool use_xattr;

public:
	DelegateHttpRequest(EventLoop &_event_loop, struct pool &_pool,
			    const char *_path, const char *_content_type,
			    bool _use_xattr,
			    HttpResponseHandler &_handler) noexcept
		:event_loop(_event_loop), pool(_pool),
		 path(_path), content_type(_content_type),
		 handler(_handler),
		 use_xattr(_use_xattr) {}

	void Open(StockMap &stock, const char *helper,
		  const ChildOptions &options,
		  CancellablePointer &cancel_ptr) noexcept {
		delegate_stock_open(&stock, pool,
				    helper, options, path,
				    *this, cancel_ptr);
	}

private:
	/* virtual methods from class DelegateHandler */
	void OnDelegateSuccess(UniqueFileDescriptor fd) noexcept override;

	void OnDelegateError(std::exception_ptr ep) noexcept override {
		handler.InvokeError(ep);
	}
};

void
DelegateHttpRequest::OnDelegateSuccess(UniqueFileDescriptor fd) noexcept
{
	struct statx st;
	if (statx(fd.Get(), "", AT_EMPTY_PATH,
		  STATX_TYPE|STATX_MTIME|STATX_INO|STATX_SIZE, &st) < 0) {
		handler.InvokeError(std::make_exception_ptr(FmtErrno("Failed to stat {}", path)));
		return;
	}

	if (!S_ISREG(st.stx_mode)) {
		handler.InvokeResponse(pool, HttpStatus::NOT_FOUND,
				       "Not a regular file");
		return;
	}

	/* XXX handle if-modified-since, ... */

	auto response_headers = static_response_headers(pool, fd, st,
							content_type,
							use_xattr);

	auto *shared_fd = NewFromPool<SharedFd>(pool, std::move(fd));

	handler.InvokeResponse(HttpStatus::OK,
			       std::move(response_headers),
			       istream_file_fd_new(event_loop, pool, path,
						   shared_fd->Get(), *shared_fd,
						   0, st.stx_size));
}

void
delegate_stock_request(EventLoop &event_loop, StockMap &stock,
		       struct pool &pool,
		       const char *helper,
		       const ChildOptions &options,
		       const char *path, const char *content_type,
		       bool use_xattr,
		       HttpResponseHandler &handler,
		       CancellablePointer &cancel_ptr) noexcept
{
	auto get = NewFromPool<DelegateHttpRequest>(pool, event_loop, pool,
						    path, content_type, use_xattr,
						    handler);
	get->Open(stock, helper, options, cancel_ptr);
}
