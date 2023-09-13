// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "FileHeaders.hxx"
#include "file/Address.hxx"
#include "Request.hxx"
#include "Instance.hxx"
#include "memory/GrowingBuffer.hxx"
#include "http/HeaderWriter.hxx"
#include "http/PHeaderUtil.hxx"
#include "http/Headers.hxx"
#include "http/Method.hxx"
#include "http/IncomingRequest.hxx"
#include "istream/FileIstream.hxx"
#include "istream/FdIstream.hxx"
#include "pool/pool.hxx"
#include "translation/Vary.hxx"
#include "lib/fmt/SystemError.hxx"
#include "io/Open.hxx"
#include "util/StringCompare.hxx"

#ifdef HAVE_URING
#include "istream/UringIstream.hxx"
#include "istream/UringSpliceIstream.hxx"
#include "io/UringOpen.hxx"
#include "io/UringOpenStat.hxx"
#include "event/uring/Manager.hxx"
#endif

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

using std::string_view_literals::operator""sv;

void
Request::DispatchFile(const char *path, UniqueFileDescriptor fd,
		      const struct statx &st,
		      const struct file_request &file_request) noexcept
{
	const TranslateResponse &tr = *translate.response;
	const auto &address = *handler.file.address;

	const char *override_content_type = translate.content_type;
	if (override_content_type == nullptr)
		override_content_type = address.content_type;

	HttpHeaders headers;
	GrowingBuffer &headers2 = headers.GetBuffer();
	file_response_headers(headers2,
			      instance.event_loop.GetSystemClockCache(),
			      override_content_type,
			      fd, st,
			      tr.GetExpiresRelative(HasQueryString()),
			      IsProcessorFirst(),
			      instance.config.use_xattr);
	write_translation_vary_header(headers2, tr);

	auto status = tr.status == HttpStatus{} ? HttpStatus::OK : tr.status;

	/* generate the Content-Range header */

	header_write(headers2, "accept-ranges", "bytes");

	off_t start_offset = 0, end_offset = st.stx_size;

	switch (file_request.range.type) {
	case HttpRangeRequest::Type::NONE:
		break;

	case HttpRangeRequest::Type::VALID:
		start_offset = file_request.range.skip;
		end_offset = file_request.range.size;

		status = HttpStatus::PARTIAL_CONTENT;

		headers.contains_content_range = true;
		header_write_begin(headers2, "content-range"sv);
		headers2.Fmt("bytes {}-{}/{}",
			     file_request.range.skip,
			     file_request.range.size - 1,
			     st.stx_size);
		header_write_finish(headers2);
		break;

	case HttpRangeRequest::Type::INVALID:
		status = HttpStatus::REQUESTED_RANGE_NOT_SATISFIABLE;

		headers.contains_content_range = true;
		header_write_begin(headers2, "content-range"sv);
		headers2.Fmt("bytes */{}", st.stx_size);
		header_write_finish(headers2);

		fd.Close();
		DispatchResponse(status, std::move(headers), nullptr);
		return;
	}

	/* finished, dispatch this response */

	DispatchResponse(status, std::move(headers),
#ifdef HAVE_URING
			 instance.uring
			 ? (IsDirect()
			    /* if this response is going to be
			       transmitted directly, use splice() with
			       io_uring instead of sendfile() to avoid
			       getting blocked by slow disk (or
			       network filesystem) I/O */
			    ? NewUringSpliceIstream(*instance.uring, pool, path,
						    std::move(fd),
						    start_offset, end_offset)
			    : NewUringIstream(*instance.uring, pool, path,
					      std::move(fd),
					      start_offset, end_offset))
			 :
#endif
			 istream_file_fd_new(instance.event_loop, pool, path,
					     std::move(fd),
					     start_offset, end_offset));
}

bool
Request::DispatchCompressedFile(const char *path, FileDescriptor fd,
				const struct statx &st,
				const char *encoding) noexcept
{
	const TranslateResponse &tr = *translate.response;
	const auto &address = *handler.file.address;

	/* open compressed file */

	UniqueFileDescriptor compressed_fd;
	if (!compressed_fd.OpenReadOnly(handler.file.base, path))
		return false;

	struct statx st2;
	if (statx(compressed_fd.Get(), "", AT_EMPTY_PATH,
		  STATX_TYPE|STATX_MTIME|STATX_INO|STATX_SIZE, &st2) < 0 ||
	    !S_ISREG(st2.stx_mode))
		return false;

	/* response headers with information from uncompressed file */

	const char *override_content_type = translate.content_type;
	if (override_content_type == nullptr)
		override_content_type = address.content_type;

	HttpHeaders headers;
	GrowingBuffer &headers2 = headers.GetBuffer();
	file_response_headers(headers2,
			      instance.event_loop.GetSystemClockCache(),
			      override_content_type,
			      fd, st,
			      tr.GetExpiresRelative(HasQueryString()),
			      IsProcessorFirst(),
			      instance.config.use_xattr);
	write_translation_vary_header(headers2, tr);

	headers.contains_content_encoding = true;
	header_write(headers2, "content-encoding", encoding);
	header_write(headers2, "vary", "accept-encoding");

	/* finished, dispatch this response */

	HttpStatus status = tr.status == HttpStatus{}
		? HttpStatus::OK
		: tr.status;
	DispatchResponse(status, std::move(headers),
#ifdef HAVE_URING
			 instance.uring
			 ? NewUringIstream(*instance.uring, pool, path,
					   std::move(compressed_fd),
					   0, st2.stx_size)
			 :
#endif
			 istream_file_fd_new(instance.event_loop, pool,
					     path, std::move(compressed_fd),
					     0, st2.stx_size));
	return true;
}

bool
Request::CheckCompressedFile(const char *path, FileDescriptor fd,
			     const struct statx &st,
			     const char *encoding) noexcept
{
	return path != nullptr &&
		http_client_accepts_encoding(request.headers, encoding) &&
		DispatchCompressedFile(path, fd, st, encoding);
}

bool
Request::CheckAutoCompressedFile(const char *path, FileDescriptor fd,
				 const struct statx &st,
				 const char *encoding, const char *suffix) noexcept
{
	assert(encoding != nullptr);
	assert(path != nullptr);
	assert(suffix != nullptr);
	assert(*suffix == '.');
	assert(suffix[1] != 0);

	if (!http_client_accepts_encoding(request.headers, encoding))
		return false;

	const AllocatorPtr alloc(pool);
	const char *compressed_path = alloc.Concat(path, suffix);
	return DispatchCompressedFile(compressed_path, fd, st, encoding);
}

inline bool
Request::MaybeEmulateModAuthEasy(const FileAddress &address,
				 UniqueFileDescriptor &fd,
				 const struct statx &st) noexcept
{
	assert(S_ISREG(st.stx_mode));

	if (!instance.config.emulate_mod_auth_easy)
		return false;

	if (IsTransformationEnabled())
		return false;

	const char *base = address.base != nullptr
		? address.base
		: address.path;

	if (!StringStartsWith(base, "/var/www/vol"))
		return false;

	if (strstr(base, "/pr_0001/public_html") == nullptr)
		return false;

	return EmulateModAuthEasy(address, fd, st);
}

inline void
Request::OnBaseOpen(FileDescriptor fd, SharedLease lease) noexcept
{
	const auto &address = *handler.file.address;
	assert(address.base != nullptr);

	handler.file.base = fd;
	handler.file.base_lease = std::move(lease);

	HandleFileAddressAfterBase(address);
}

#ifdef HAVE_URING

void
Request::OnOpenStat(UniqueFileDescriptor fd,
		    struct statx &st) noexcept
{
	HandleFileAddress(*handler.file.address, std::move(fd), st);
}

void
Request::OnOpenStatError(std::exception_ptr e) noexcept
{
	LogDispatchError(std::move(e));
}

#endif

void
Request::HandleFileAddress(const FileAddress &address) noexcept
{
	handler.file.address = &address;

	assert(address.path != nullptr);

	const char *const path = address.path;

	if (address.delegate != nullptr) {
		HandleDelegateAddress(*address.delegate, path);
		return;
	}

	/* open the BASE directory */

	if (address.base != nullptr) {
		instance.fd_cache.Get(address.base, O_PATH|O_DIRECTORY,
				      BIND_THIS_METHOD(OnBaseOpen),
				      BIND_THIS_METHOD(OnBaseOpenError),
				      cancel_ptr);
		return;
	} else
		handler.file.base = FileDescriptor(AT_FDCWD);

	/* open the file */

	HandleFileAddressAfterBase(address);
}

void
Request::HandleFileAddressAfterBase(const FileAddress &address) noexcept
{
	const char *const path = address.path;

#ifdef HAVE_URING
	if (instance.uring) {
		UringOpenStat(*instance.uring, pool,
			      handler.file.base,
			      path,
			      *this, cancel_ptr);
		return;
	}
#endif

	UniqueFileDescriptor fd;
	struct statx st;

	try {
		fd = OpenReadOnly(handler.file.base, path);
		if (statx(fd.Get(), "", AT_EMPTY_PATH,
			  STATX_TYPE|STATX_MTIME|STATX_INO|STATX_SIZE,
			  &st) < 0)
			throw FmtErrno("Failed to stat {}", path);
	} catch (...) {
		LogDispatchError(std::current_exception());
		return;
	}

	HandleFileAddress(address, std::move(fd), st);
}

void
Request::HandleFileAddress(const FileAddress &address,
			   UniqueFileDescriptor fd,
			   const struct statx &st) noexcept
{
	/* check request method */

	if (request.method != HttpMethod::HEAD &&
	    request.method != HttpMethod::GET &&
	    !processor_focus) {
		DispatchMethodNotAllowed("GET, HEAD");
		return;
	}

	/* check file type */

	if (S_ISCHR(st.stx_mode)) {
		/* allow character devices, but skip range etc. */
		DispatchResponse(HttpStatus::OK, {},
				 NewFdIstream(instance.event_loop,
					      pool, address.path,
					      std::move(fd),
					      FdType::FD_CHARDEV));
		return;
	}

	if (!S_ISREG(st.stx_mode)) {
		instance.uring.Close(fd.Release());
		DispatchError(HttpStatus::NOT_FOUND, "Not a regular file");
		return;
	}

	if (MaybeEmulateModAuthEasy(address, fd, st)) {
		if (fd.IsDefined())
			instance.uring.Close(fd.Release());
		return;
	}

	struct file_request file_request(st.stx_size);

	/* request options */

	if (!EvaluateFileRequest(fd, st, file_request)) {
		instance.uring.Close(fd.Release());
		return;
	}

	/* precompressed? */

	if (file_request.range.type == HttpRangeRequest::Type::NONE &&
	    !IsTransformationEnabled() &&
	    (((address.auto_brotli_path || translate.auto_brotli_path) &&
	      CheckAutoCompressedFile(address.path, fd, st, "br", ".br")) ||
	     ((address.auto_gzipped || translate.auto_gzipped) &&
	      CheckAutoCompressedFile(address.path, fd, st, "gzip", ".gz")) ||
	     CheckCompressedFile(address.gzipped, fd, st, "gzip"))) {
		instance.uring.Close(fd.Release());
		return;
	}

	/* build the response */

	DispatchFile(address.path, std::move(fd), st, file_request);
}

static bool
PathExists(const FileAddress &address)
{
	// TODO: use uring

	struct statx st;

	if (address.base != nullptr) {
		auto base = OpenPath(address.base);
		return statx(base.Get(), address.path,
			     AT_SYMLINK_NOFOLLOW|AT_STATX_SYNC_AS_STAT,
			     0, &st) == 0;
	} else {
		return statx(AT_FDCWD, address.path,
			     AT_SYMLINK_NOFOLLOW|AT_STATX_SYNC_AS_STAT,
			     0, &st) == 0;
	}
}

static constexpr HttpStatus
ErrnoToHttpStatus(int e) noexcept
{
	switch (e) {
	case ENOENT:
	case ENOTDIR:
	case ELOOP: /* RESOLVE_NO_SYMLINKS failed */
	case EXDEV: /* RESOLVE_BENEATH failed */
		return HttpStatus::NOT_FOUND;

	case EACCES:
	case EPERM:
		return HttpStatus::FORBIDDEN;

	case ECONNREFUSED:
	case ENETUNREACH:
	case EHOSTUNREACH:
	case ETIMEDOUT:
		return HttpStatus::BAD_GATEWAY;

	default:
		return HttpStatus::INTERNAL_SERVER_ERROR;
	}
}

void
Request::HandlePathExists(const FileAddress &address) noexcept
{
	try {
		translate.request.status = PathExists(address)
			? HttpStatus::OK
			: ErrnoToHttpStatus(errno);
		translate.request.path_exists = true;
		SubmitTranslateRequest();
	} catch (...) {
		LogDispatchError(std::current_exception());
	}
}
