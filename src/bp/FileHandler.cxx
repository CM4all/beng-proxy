/*
 * Copyright 2007-2021 CM4all GmbH
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

#include "FileHeaders.hxx"
#include "file_address.hxx"
#include "Request.hxx"
#include "Instance.hxx"
#include "http/HeaderWriter.hxx"
#include "http/PHeaderUtil.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "istream/FileIstream.hxx"
#include "istream/FdIstream.hxx"
#include "istream/UringIstream.hxx"
#include "pool/pool.hxx"
#include "translation/Vary.hxx"
#include "system/Error.hxx"
#include "system/KernelVersion.hxx"
#include "io/Open.hxx"
#include "util/StringCompare.hxx"

#ifdef HAVE_URING
#include "io/UringOpenStat.hxx"
#include "event/uring/Manager.hxx"
#endif

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

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
			      tr.expires_relative,
			      IsProcessorFirst());
	write_translation_vary_header(headers2, tr);

	http_status_t status = tr.status == 0 ? HTTP_STATUS_OK : tr.status;

	/* generate the Content-Range header */

	header_write(headers2, "accept-ranges", "bytes");

	off_t start_offset = 0, end_offset = st.stx_size;

	switch (file_request.range.type) {
	case HttpRangeRequest::Type::NONE:
		break;

	case HttpRangeRequest::Type::VALID:
		start_offset = file_request.range.skip;
		end_offset = file_request.range.size;

		status = HTTP_STATUS_PARTIAL_CONTENT;

		header_write(headers2, "content-range",
			     p_sprintf(&pool, "bytes %lu-%lu/%lu",
				       (unsigned long)file_request.range.skip,
				       (unsigned long)(file_request.range.size - 1),
				       (unsigned long)st.stx_size));
		break;

	case HttpRangeRequest::Type::INVALID:
		status = HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE;

		header_write(headers2, "content-range",
			     p_sprintf(&pool, "bytes */%lu",
				       (unsigned long)st.stx_size));

		fd.Close();
		DispatchResponse(status, std::move(headers), nullptr);
		return;
	}

	/* finished, dispatch this response */

	DispatchResponse(status, std::move(headers),
#ifdef HAVE_URING
			 instance.uring
			 ? NewUringIstream(*instance.uring, pool, path,
					   std::move(fd),
					   start_offset, end_offset)
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

	try {
		compressed_fd = OpenReadOnly(handler.file.base, path);
	} catch (...) {
		return false;
	}

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
			      tr.expires_relative,
			      IsProcessorFirst());
	write_translation_vary_header(headers2, tr);

	header_write(headers2, "content-encoding", encoding);
	header_write(headers2, "vary", "accept-encoding");

	/* finished, dispatch this response */

	compressed = true;

	http_status_t status = tr.status == 0 ? HTTP_STATUS_OK : tr.status;
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

	/* check request */

	if (request.method != HTTP_METHOD_HEAD &&
	    request.method != HTTP_METHOD_GET &&
	    !processor_focus) {
		DispatchMethodNotAllowed("GET, HEAD");
		return;
	}

	if (address.delegate != nullptr) {
		HandleDelegateAddress(*address.delegate, path);
		return;
	}

	/* open the file */

	if (address.base != nullptr) {
		// TODO: use uring
		try {
			handler.file.base =
				handler.file.base_ = IsKernelVersionOrNewer({5, 6, 13})
				? OpenPath(address.base)
				/* O_PATH file descriptors are broken
				   in io_uring until at least 5.6.12,
				   see
				   https://lkml.org/lkml/2020/5/7/1287 */
				: OpenDirectory(address.base);
		} catch (...) {
			LogDispatchError(std::current_exception());
			return;
		}
	} else
		handler.file.base = FileDescriptor(AT_FDCWD);

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
			throw FormatErrno("Failed to stat %s", path);
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
	/* check file type */

	if (S_ISCHR(st.stx_mode)) {
		/* allow character devices, but skip range etc. */
		DispatchResponse(HTTP_STATUS_OK, {},
				 NewFdIstream(instance.event_loop,
					      pool, address.path,
					      std::move(fd),
					      FdType::FD_CHARDEV));
		return;
	}

	if (!S_ISREG(st.stx_mode)) {
		DispatchError(HTTP_STATUS_NOT_FOUND, "Not a regular file");
		return;
	}

	if (MaybeEmulateModAuthEasy(address, fd, st))
		return;

	struct file_request file_request(st.stx_size);

	/* request options */

	if (!EvaluateFileRequest(fd, st, file_request))
		return;

	/* precompressed? */

	if (!compressed &&
	    file_request.range.type == HttpRangeRequest::Type::NONE &&
	    !IsTransformationEnabled() &&
	    (CheckCompressedFile(address.deflated, fd, st, "deflate") ||
	     (address.auto_gzipped &&
	      CheckAutoCompressedFile(address.path, fd, st, "gzip", ".gz")) ||
	     CheckCompressedFile(address.gzipped, fd, st, "gzip")))
		return;

	/* build the response */

	DispatchFile(address.path, std::move(fd), st, file_request);
}
