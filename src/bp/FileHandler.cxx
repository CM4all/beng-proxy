// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Precompressed.hxx"
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
#include "io/FileAt.hxx"
#include "io/Open.hxx"
#include "util/StringCompare.hxx"

#ifdef HAVE_URING
#include "istream/UringIstream.hxx"
#include "istream/UringSpliceIstream.hxx"
#include "event/uring/Manager.hxx"
#endif

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

using std::string_view_literals::operator""sv;

inline void
Request::Handler::File::Close(UringGlue &uring) noexcept
{
	open_address = nullptr;
	error = 0;

	if (fd.IsDefined())
		uring.Close(fd.Release());
}

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

inline bool
Request::DispatchCompressedFile(const char *path, FileDescriptor fd,
				const struct statx &st,
				const char *encoding,
				UniqueFileDescriptor compressed_fd,
				off_t compressed_size) noexcept
{
	const TranslateResponse &tr = *translate.response;
	const auto &address = *handler.file.address;

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
					   0, compressed_size)
			 :
#endif
			 istream_file_fd_new(instance.event_loop, pool,
					     path, std::move(compressed_fd),
					     0, compressed_size));
	return true;
}

inline bool
Request::CheckCompressedFile(const char *path, const char *encoding) noexcept
{
	assert(path != nullptr);
	assert(encoding != nullptr);

	if (!http_client_accepts_encoding(request.headers, encoding))
		return false;

	auto &p = *handler.file.precompressed;

	const AllocatorPtr alloc(pool);
	p.compressed_path = path;
	p.encoding = encoding;
	instance.uring.OpenStat(alloc,
				{handler.file.base, StripBase(p.compressed_path)},
				BIND_THIS_METHOD(OnPrecompressedOpenStat),
				BIND_THIS_METHOD(OnPrecompressedOpenStatError),
				cancel_ptr);
	return true;
}

inline bool
Request::CheckAutoCompressedFile(const char *path, const char *encoding,
				 const char *suffix) noexcept
{
	assert(encoding != nullptr);
	assert(path != nullptr);
	assert(suffix != nullptr);
	assert(*suffix == '.');
	assert(suffix[1] != 0);

	if (!http_client_accepts_encoding(request.headers, encoding))
		return false;

	auto &p = *handler.file.precompressed;

	const AllocatorPtr alloc(pool);
	p.compressed_path = alloc.Concat(path, suffix);
	p.encoding = encoding;
	instance.uring.OpenStat(alloc,
				{handler.file.base, StripBase(p.compressed_path)},
				BIND_THIS_METHOD(OnPrecompressedOpenStat),
				BIND_THIS_METHOD(OnPrecompressedOpenStatError),
				cancel_ptr);
	return true;
}

inline void
Request::OnPrecompressedOpenStat(UniqueFileDescriptor fd,
				 struct statx &st) noexcept
{
	if (!S_ISREG(st.stx_mode)) {
		ProbeNextPrecompressed();
		return;
	}

	const auto &p = *handler.file.precompressed;

	DispatchCompressedFile(p.compressed_path, p.original_fd, p.original_st,
			       p.encoding,
			       std::move(fd), st.stx_size);
}

inline void
Request::OnPrecompressedOpenStatError([[maybe_unused]] int error) noexcept
{
	ProbeNextPrecompressed();
}

void
Request::ProbeNextPrecompressed() noexcept
{
	const auto &address = *handler.file.address;
	auto &p = *handler.file.precompressed;

	switch (p.state) {
#ifdef HAVE_BROTLI
	case Handler::File::Precompressed::AUTO_BROTLI:
		p.state = Handler::File::Precompressed::AUTO_GZIPPED;

		if ((address.auto_brotli_path || translate.auto_brotli_path) &&
		    CheckAutoCompressedFile(address.path, "br", ".br"))
			return;
#endif // HAVE_BROTLI

		// fall through

	case Handler::File::Precompressed::AUTO_GZIPPED:
		p.state = Handler::File::Precompressed::GZIPPED;

		if ((address.auto_gzipped || translate.auto_gzipped) &&
		    CheckAutoCompressedFile(address.path, "gzip", ".gz"))
			return;

		// fall through

	case Handler::File::Precompressed::GZIPPED:
		p.state = Handler::File::Precompressed::END;

		if (address.gzipped != nullptr &&
		    CheckCompressedFile(address.gzipped, "gzip"))
			return;

		// fall through
	case Handler::File::Precompressed::END:
		break;
	}

	const struct file_request file_request(p.original_st.stx_size);
	DispatchFile(address.path, std::move(p.original_fd), p.original_st,
		     file_request);
}

inline void
Request::ProbePrecompressed(UniqueFileDescriptor fd,
			    const struct statx &st) noexcept
{
	handler.file.precompressed = UniquePoolPtr<Request::Handler::File::Precompressed>::Make(pool, std::move(fd), st);
	ProbeNextPrecompressed();
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
Request::OnOpenStat(UniqueFileDescriptor fd,
		    struct statx &st) noexcept
{
	HandleFileAddress(*handler.file.address, std::move(fd), st);
}

inline void
Request::OnOpenStatError(int error) noexcept
{
	LogDispatchErrno(error, "Failed to open file");
}

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

	if (&address == handler.file.open_address) {
		assert(handler.file.fd.IsDefined() || handler.file.error != 0);

		if (handler.file.fd.IsDefined())
			/* file has already been opened */
			HandleFileAddress(address, std::move(handler.file.fd),
					  handler.file.stx);
		else
			OnOpenStatError(handler.file.error);
	} else
		OpenBase(address, &Request::HandleFileAddressAfterBase);
}

void
Request::HandleFileAddressAfterBase(FileDescriptor base) noexcept
{
	instance.uring.OpenStat(pool,
				{base, StripBase(handler.file.address->path)},
				BIND_THIS_METHOD(OnOpenStat),
				BIND_THIS_METHOD(OnOpenStatError),
				cancel_ptr);
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
	    !IsTransformationEnabled()) {
		ProbePrecompressed(std::move(fd), st);
		return;
	}

	/* build the response */

	DispatchFile(address.path, std::move(fd), st, file_request);
}

void
Request::OnStatOpenStatSuccess(UniqueFileDescriptor fd,
			       struct statx &st) noexcept
{
	assert(!handler.file.fd.IsDefined());
	assert(handler.file.error == 0);

	handler.file.fd = std::move(fd);
	handler.file.stx = st;
	handler.file.open_address = handler.file.address;

	(this->*handler.file.on_stat_success)(st);
}

void
Request::OnStatOpenStatError(int error) noexcept
{
	assert(!handler.file.fd.IsDefined());
	assert(handler.file.error == 0);

	handler.file.error = error;
	handler.file.open_address = handler.file.address;

	(this->*handler.file.on_stat_error)(error);
}

void
Request::StatFileAddressAfterBase(FileDescriptor base) noexcept
{
	assert(!handler.file.fd.IsDefined());

	const FileAddress &address = *handler.file.address;

	instance.uring.OpenStat(pool,
				{base, StripBase(address.path)},
				BIND_THIS_METHOD(OnStatOpenStatSuccess),
				BIND_THIS_METHOD(OnStatOpenStatError),
				cancel_ptr);
}

void
Request::StatFileAddress(const FileAddress &address,
			 Handler::File::StatSuccessCallback on_success,
			 Handler::File::StatErrorCallback on_error) noexcept
{
	if (&address == handler.file.open_address) {
		assert(handler.file.fd.IsDefined() || handler.file.error != 0);

		if (handler.file.fd.IsDefined())
			(this->*on_success)(handler.file.stx);
		else
			(this->*on_error)(handler.file.error);
	} else {
		handler.file.Close(instance.uring);

		handler.file.address = &address;
		handler.file.on_stat_success = on_success;
		handler.file.on_stat_error = on_error;

		OpenBase(address, &Request::StatFileAddressAfterBase);
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

	case ENAMETOOLONG:
		return HttpStatus::REQUEST_URI_TOO_LONG;

	default:
		return HttpStatus::INTERNAL_SERVER_ERROR;
	}
}

void
Request::HandlePathExists(const FileAddress &address) noexcept
{
	handler.file.address = &address;

	StatFileAddress(address,
			&Request::OnPathExistsStat,
			&Request::OnPathExistsStatError);
}

inline void
Request::OnPathExistsStat([[maybe_unused]] const struct statx &st) noexcept
{
	translate.request.status = HttpStatus::OK;
	translate.request.path_exists = true;
	SubmitTranslateRequest();
}

inline void
Request::OnPathExistsStatError(int error) noexcept
{
	translate.request.status = ErrnoToHttpStatus(error);
	translate.request.path_exists = true;
	SubmitTranslateRequest();
}
