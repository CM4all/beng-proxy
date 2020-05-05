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

#include "FileHeaders.hxx"
#include "file_address.hxx"
#include "Request.hxx"
#include "Instance.hxx"
#include "GenerateResponse.hxx"
#include "http/HeaderWriter.hxx"
#include "http/PHeaderUtil.hxx"
#include "http/Date.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "istream/FileIstream.hxx"
#include "istream/FdIstream.hxx"
#include "istream/UringIstream.hxx"
#include "istream/istream.hxx"
#include "pool/pool.hxx"
#include "translation/Vary.hxx"
#include "system/Error.hxx"
#include "io/Open.hxx"
#include "util/DecimalFormat.h"
#include "util/StringCompare.hxx"

#ifdef HAVE_URING
#include "io/UringOpenStat.hxx"
#include "event/uring/Manager.hxx"
#include <sys/sysmacros.h> // for makedev()
#endif

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

void
Request::DispatchFile(const char *path, UniqueFileDescriptor fd,
		      const struct stat &st,
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

	off_t start_offset = 0, end_offset = st.st_size;

	switch (file_request.range.type) {
	case HttpRangeRequest::Type::NONE:
		break;

	case HttpRangeRequest::Type::VALID:
		start_offset = file_request.range.skip;
		end_offset = start_offset + file_request.range.size;

		status = HTTP_STATUS_PARTIAL_CONTENT;

		header_write(headers2, "content-range",
			     p_sprintf(&pool, "bytes %lu-%lu/%lu",
				       (unsigned long)file_request.range.skip,
				       (unsigned long)(file_request.range.size - 1),
				       (unsigned long)st.st_size));
		break;

	case HttpRangeRequest::Type::INVALID:
		status = HTTP_STATUS_REQUESTED_RANGE_NOT_SATISFIABLE;

		header_write(headers2, "content-range",
			     p_sprintf(&pool, "bytes */%lu",
				       (unsigned long)st.st_size));

		fd.Close();
		break;
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
				const struct stat &st,
				const char *encoding) noexcept
{
	const TranslateResponse &tr = *translate.response;
	const auto &address = *handler.file.address;

	/* open compressed file */

	UniqueFileDescriptor compressed_fd;
	struct stat st2;

	try {
		compressed_fd = OpenReadOnly(path);
	} catch (...) {
		return false;
	}

	if (fstat(compressed_fd.Get(), &st2) < 0 || S_ISREG(st2.st_mode))
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
					   0, st2.st_size)
			 :
#endif
			 istream_file_fd_new(instance.event_loop, pool,
					     path, std::move(compressed_fd),
					     0, st2.st_size));
	return true;
}

bool
Request::CheckCompressedFile(const char *path, FileDescriptor fd,
			     const struct stat &st,
			     const char *encoding) noexcept
{
	return path != nullptr &&
		http_client_accepts_encoding(request.headers, encoding) &&
		DispatchCompressedFile(path, fd, st, encoding);
}

bool
Request::CheckAutoCompressedFile(const char *path, FileDescriptor fd,
				 const struct stat &st,
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
				 const struct stat &st) noexcept
{
	assert(S_ISREG(st.st_mode));

	if (!instance.config.emulate_mod_auth_easy)
		return false;

	if (IsTransformationEnabled())
		return false;

	if (!StringStartsWith(address.path, "/var/www/vol"))
		return false;

	if (strstr(address.path, "/pr_0001/public_html/") == nullptr)
		return false;

	return EmulateModAuthEasy(address, fd, st);
}

#ifdef HAVE_URING

void
Request::OnOpenStat(UniqueFileDescriptor fd,
		    struct statx &stx) noexcept
{
	/* copy the struct statx to an old-style struct stat (this can
	   be removed once be migrate everything to struct statx) */
	struct stat st;
	st.st_mode = stx.stx_mode;
	st.st_size = stx.stx_size;
	st.st_mtime = stx.stx_mtime.tv_sec;
	st.st_dev = makedev(stx.stx_dev_major, stx.stx_dev_minor);
	st.st_ino = stx.stx_ino;

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
	assert(address.delegate == nullptr);

	const char *const path = address.path;

	/* check request */

	if (request.method != HTTP_METHOD_HEAD &&
	    request.method != HTTP_METHOD_GET &&
	    !processor_focus) {
		method_not_allowed(*this, "GET, HEAD");
		return;
	}

	/* open the file */

#ifdef HAVE_URING
	if (instance.uring) {
		UringOpenStat(*instance.uring, pool, path,
			      *this, cancel_ptr);
		return;
	}
#endif

	UniqueFileDescriptor fd;
	struct stat st;

	try {
		fd = OpenReadOnly(path);
		if (fstat(fd.Get(), &st) < 0)
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
			   const struct stat &st) noexcept
{
	/* check file type */

	if (S_ISCHR(st.st_mode)) {
		/* allow character devices, but skip range etc. */
		DispatchResponse(HTTP_STATUS_OK, {},
				 NewFdIstream(instance.event_loop,
					      pool, address.path,
					      std::move(fd),
					      FdType::FD_CHARDEV));
		return;
	}

	if (!S_ISREG(st.st_mode)) {
		DispatchResponse(HTTP_STATUS_INTERNAL_SERVER_ERROR,
				 "Not a regular file");
		return;
	}

	if (MaybeEmulateModAuthEasy(address, fd, st))
		return;

	struct file_request file_request(st.st_size);

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
