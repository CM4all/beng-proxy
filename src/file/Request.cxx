// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "Request.hxx"
#include "Headers.hxx"
#include "http/ResponseHandler.hxx"
#include "strmap.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/FileIstream.hxx"
#include "istream/UringIstream.hxx"
#include "pool/pool.hxx"
#include "lib/fmt/SystemError.hxx"
#include "system/Error.hxx"
#include "io/FileAt.hxx"
#include "io/Open.hxx"
#include "io/SharedFd.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "http/Status.hxx"

#ifdef HAVE_URING
#include "io/uring/Handler.hxx"
#include "io/uring/OpenStat.hxx"
#include "util/Cancellable.hxx"
#include <memory>
#endif

#include <assert.h>
#include <sys/stat.h>
#include <fcntl.h>

#ifdef HAVE_URING

class UringStaticFileGet final : Uring::OpenStatHandler, Cancellable {
	struct pool &pool;

	UniqueFileDescriptor base;

	const char *const path;
	const char *const content_type;

	std::unique_ptr<Uring::OpenStat> open_stat;

	HttpResponseHandler &handler;

	const bool use_xattr;

public:
	UringStaticFileGet(Uring::Queue &uring,
			   struct pool &_pool,
			   UniqueFileDescriptor &&_base,
			   const char *_path,
			   const char *_content_type,
			   bool _use_xattr,
			   HttpResponseHandler &_handler) noexcept
		:pool(_pool),
		 base(std::move(_base)), // TODO: use io_uring to open it
		 path(_path),
		 content_type(_content_type),
		 open_stat(new Uring::OpenStat(uring, *this)),
		 handler(_handler),
		 use_xattr(_use_xattr){}

	void Start(CancellablePointer &cancel_ptr) noexcept {
		cancel_ptr = *this;

		if (base.IsDefined())
			open_stat->StartOpenStatReadOnlyBeneath({base, path});
		else
			open_stat->StartOpenStatReadOnly(path);
	}

private:
	void Destroy() noexcept {
		this->~UringStaticFileGet();
	}

	/* virtual methods from class Cancellable */
	void Cancel() noexcept override {
		/* keep the Uring::OpenStat allocated until the kernel
		   finishes the operation, or else the kernel may
		   overwrite the memory when something else occupies
		   it; also, the canceled object will take care for
		   closing the new file descriptor */
		open_stat->Cancel();
		open_stat.release();

		Destroy();
	}

	/* virtual methods from class Uring::OpenStatHandler */
	void OnOpenStat(UniqueFileDescriptor fd,
			struct statx &st) noexcept override;

	void OnOpenStatError(int error) noexcept override {
		auto &_handler = handler;
		Destroy();
		_handler.InvokeError(std::make_exception_ptr(MakeErrno(error, "Failed to open file")));
	}
};

void
UringStaticFileGet::OnOpenStat(UniqueFileDescriptor fd,
			       struct statx &stx) noexcept
{
	const char *_content_type = content_type;
	auto &_handler = handler;

	/* delay destruction, because this object owns the
	   memory pointed to by "st" */
	const auto operation = std::move(open_stat);

	Destroy();

	if (!S_ISREG(stx.stx_mode)) {
		_handler.InvokeResponse(pool, HttpStatus::NOT_FOUND,
					"Not a regular file");
		return;
	}

	auto headers = static_response_headers(pool, fd, stx, _content_type, use_xattr);

	auto *shared_fd = new SharedFd(std::move(fd));

	_handler.InvokeResponse(HttpStatus::OK,
				std::move(headers),
				NewUringIstream(operation->GetQueue(), pool,
						path, shared_fd->Get(), *shared_fd,
						0, stx.stx_size));
}

#endif

void
static_file_get(EventLoop &event_loop,
#ifdef HAVE_URING
		Uring::Queue *uring,
#endif
		struct pool &pool,
		const char *_base,
		const char *path, const char *content_type,
		bool use_xattr,
		HttpResponseHandler &handler, CancellablePointer &cancel_ptr)
{
	assert(path != nullptr);

	UniqueFileDescriptor base;

	if (_base != nullptr) {
		try {
			base = OpenPath(_base);
		} catch (...) {
			handler.InvokeError(std::current_exception());
			return;
		}
	}

#ifdef HAVE_URING
	if (uring != nullptr) {
		auto *o = NewFromPool<UringStaticFileGet>(pool,
							  *uring, pool,
							  std::move(base),
							  path, content_type,
							  use_xattr,
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
		fd = OpenReadOnly(base.IsDefined() ? base : FileDescriptor::Undefined(),
				  path, O_NOFOLLOW);
		if (statx(fd.Get(), "", AT_EMPTY_PATH,
			  STATX_TYPE|STATX_MTIME|STATX_INO|STATX_SIZE,
			  &st) < 0)
			throw FmtErrno("Failed to stat {}", path);
	} catch (...) {
		handler.InvokeError(std::current_exception());
		return;
	}

	if (!S_ISREG(st.stx_mode)) {
		handler.InvokeResponse(pool, HttpStatus::NOT_FOUND,
				       "Not a regular file");
		return;
	}

	auto headers = static_response_headers(pool, fd, st, content_type,
					       use_xattr);

	auto *shared_fd = NewFromPool<SharedFd>(pool, std::move(fd));

	handler.InvokeResponse(HttpStatus::OK,
			       std::move(headers),
			       istream_file_fd_new(event_loop, pool, path,
						   shared_fd->Get(), *shared_fd,
						   0, st.stx_size));
}
