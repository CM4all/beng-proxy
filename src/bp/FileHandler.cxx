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
#include "istream/istream.hxx"
#include "pool/pool.hxx"
#include "translation/Vary.hxx"
#include "util/DecimalFormat.h"
#include "util/StringCompare.hxx"

#include <assert.h>
#include <sys/stat.h>
#include <errno.h>
#include <stdlib.h>

void
Request::DispatchFile(const struct stat &st,
		      const struct file_request &file_request,
		      Istream *body) noexcept
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
			      istream_file_fd(*body), st,
			      tr.expires_relative,
			      IsProcessorFirst());
	write_translation_vary_header(headers2, tr);

	http_status_t status = tr.status == 0 ? HTTP_STATUS_OK : tr.status;

	/* generate the Content-Range header */

	header_write(headers2, "accept-ranges", "bytes");

	switch (file_request.range.type) {
	case HttpRangeRequest::Type::NONE:
		break;

	case HttpRangeRequest::Type::VALID:
		istream_file_set_range(*body, file_request.range.skip,
				       file_request.range.size);

		assert(body->GetAvailable(false) ==
		       off_t(file_request.range.size - file_request.range.skip));

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

		body->CloseUnused();
		body = nullptr;
		break;
	}

	/* finished, dispatch this response */

	DispatchResponse(status, std::move(headers),
			 UnusedIstreamPtr(body));
}

bool
Request::DispatchCompressedFile(const struct stat &st,
				Istream &body, const char *encoding,
				const char *path)
{
	const TranslateResponse &tr = *translate.response;
	const auto &address = *handler.file.address;

	/* open compressed file */

	struct stat st2;
	UnusedIstreamPtr compressed_body;

	try {
		compressed_body = UnusedIstreamPtr(istream_file_stat_new(instance.event_loop,
									 pool,
									 path, st2));
	} catch (...) {
		return false;
	}

	if (!S_ISREG(st2.st_mode))
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
			      istream_file_fd(body), st,
			      tr.expires_relative,
			      IsProcessorFirst());
	write_translation_vary_header(headers2, tr);

	header_write(headers2, "content-encoding", encoding);
	header_write(headers2, "vary", "accept-encoding");

	/* close original file */

	body.CloseUnused();

	/* finished, dispatch this response */

	compressed = true;

	http_status_t status = tr.status == 0 ? HTTP_STATUS_OK : tr.status;
	DispatchResponse(status, std::move(headers),
			 std::move(compressed_body));
	return true;
}

bool
Request::CheckCompressedFile(const struct stat &st,
			     Istream &body, const char *encoding,
			     const char *path) noexcept
{
	return path != nullptr &&
		http_client_accepts_encoding(request.headers, encoding) &&
		DispatchCompressedFile(st, body, encoding, path);
}

bool
Request::CheckAutoCompressedFile(const struct stat &st,
				 Istream &body, const char *encoding,
				 const char *path, const char *suffix) noexcept
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
	return DispatchCompressedFile(st, body, encoding, compressed_path);
}

inline bool
Request::MaybeEmulateModAuthEasy(const FileAddress &address,
				 const struct stat &st, Istream *body) noexcept
{
	if (!instance.config.emulate_mod_auth_easy)
		return false;

	if (IsTransformationEnabled())
		return false;

	if (!S_ISREG(st.st_mode))
		return false;

	if (!StringStartsWith(address.path, "/var/www/vol"))
		return false;

	if (strstr(address.path, "/pr_0001/public_html/") == nullptr)
		return false;

	return EmulateModAuthEasy(address, st, body);
}

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

	struct stat st;
	Istream *body;

	try {
		body = istream_file_stat_new(instance.event_loop, pool, path, st);
	} catch (...) {
		LogDispatchError(std::current_exception());
		return;
	}

	if (MaybeEmulateModAuthEasy(address, st, body))
		return;

	/* check file type */

	if (S_ISCHR(st.st_mode)) {
		/* allow character devices, but skip range etc. */
		DispatchResponse(HTTP_STATUS_OK, {},
				 UnusedIstreamPtr(body));
		return;
	}

	if (!S_ISREG(st.st_mode)) {
		body->CloseUnused();
		DispatchResponse(HTTP_STATUS_NOT_FOUND,
				 "Not a regular file");
		return;
	}

	struct file_request file_request(st.st_size);

	/* request options */

	if (!EvaluateFileRequest(istream_file_fd(*body), st, file_request)) {
		body->CloseUnused();
		return;
	}

	/* precompressed? */

	if (!compressed &&
	    file_request.range.type == HttpRangeRequest::Type::NONE &&
	    !IsTransformationEnabled() &&
	    (CheckCompressedFile(st, *body, "deflate", address.deflated) ||
	     (address.auto_gzipped &&
	      CheckAutoCompressedFile(st, *body, "gzip", address.path, ".gz")) ||
	     CheckCompressedFile(st, *body, "gzip", address.gzipped)))
		return;

	/* build the response */

	DispatchFile(st, file_request, body);
}
