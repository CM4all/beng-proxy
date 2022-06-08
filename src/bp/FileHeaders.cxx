/*
 * Copyright 2007-2022 CM4all GmbH
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
#include "Request.hxx"
#include "Instance.hxx"
#include "file/Headers.hxx"
#include "http/HeaderWriter.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"
#include "translation/Vary.hxx"
#include "http/List.hxx"
#include "http/Date.hxx"
#include "event/Loop.hxx"
#include "io/FileDescriptor.hxx"

#include <attr/xattr.h>

#include <assert.h>
#include <stdlib.h>
#include <sys/stat.h>

gcc_pure
static std::chrono::seconds
read_xattr_max_age(FileDescriptor fd)
{
	assert(fd.IsDefined());

	char buffer[32];
	ssize_t nbytes = fgetxattr(fd.Get(), "user.MaxAge",
				   buffer, sizeof(buffer) - 1);
	if (nbytes <= 0)
		return std::chrono::seconds::zero();

	buffer[nbytes] = 0;

	char *endptr;
	unsigned long max_age = strtoul(buffer, &endptr, 10);
	if (*endptr != 0)
		return std::chrono::seconds::zero();

	return std::chrono::seconds(max_age);
}

static void
generate_expires(GrowingBuffer &headers,
		 std::chrono::system_clock::time_point now,
		 std::chrono::system_clock::duration max_age)
{
	constexpr std::chrono::system_clock::duration max_max_age =
		std::chrono::hours(365 * 24);
	if (max_age > max_max_age)
		/* limit max_age to approximately one year */
		max_age = max_max_age;

	/* generate an "Expires" response header */
	header_write(headers, "expires",
		     http_date_format(now + max_age));
}

gcc_pure
static bool
CheckETagList(const char *list, FileDescriptor fd,
	      const struct statx &st) noexcept
{
	assert(list != nullptr);

	if (strcmp(list, "*") == 0)
		return true;

	char buffer[256];
	GetAnyETag(buffer, sizeof(buffer), fd, st);
	return http_list_contains(list, buffer);
}

static void
MakeETag(GrowingBuffer &headers, FileDescriptor fd, const struct statx &st)
{
	char buffer[512];
	GetAnyETag(buffer, sizeof(buffer), fd, st);

	header_write(headers, "etag", buffer);
}

static void
file_cache_headers(GrowingBuffer &headers,
		   const ClockCache<std::chrono::system_clock> &system_clock,
		   FileDescriptor fd, const struct statx &st,
		   std::chrono::seconds max_age)
{
	header_write(headers, "last-modified",
		     http_date_format(std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec)));

	MakeETag(headers, fd, st);

	if (max_age == std::chrono::seconds::zero() && fd.IsDefined())
		max_age = read_xattr_max_age(fd);

	if (max_age > std::chrono::seconds::zero())
		generate_expires(headers, system_clock.now(), max_age);
}

/**
 * Verifies the If-Range request header (RFC 2616 14.27).
 */
static bool
check_if_range(const char *if_range,
	       FileDescriptor fd, const struct statx &st) noexcept
{
	if (if_range == nullptr)
		return true;

	const auto t = http_date_parse(if_range);
	if (t != std::chrono::system_clock::from_time_t(-1))
		return std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec) == t;

	char etag[256];
	GetAnyETag(etag, sizeof(etag), fd, st);
	return strcmp(if_range, etag) == 0;
}

/**
 * Generate a "304 Not Modified" response.
 */
static void
DispatchNotModified(Request &request2, const TranslateResponse &tr,
		    FileDescriptor fd, const struct statx &st)
{
	HttpHeaders headers;
	auto &headers2 = headers.GetBuffer();

	file_cache_headers(headers2,
			   request2.instance.event_loop.GetSystemClockCache(),
			   fd, st, tr.GetExpiresRelative(request2.HasQueryString()));

	write_translation_vary_header(headers2, tr);

	request2.DispatchError(HTTP_STATUS_NOT_MODIFIED,
			       std::move(headers), nullptr);
}

bool
Request::EvaluateFileRequest(FileDescriptor fd, const struct statx &st,
			     struct file_request &file_request) noexcept
{
	const auto &request_headers = request.headers;
	const auto &tr = *translate.response;
	bool ignore_if_modified_since = false;

	if (tr.status == 0 && request.method == HTTP_METHOD_GET &&
	    !IsTransformationEnabled()) {
		const char *p = request_headers.Get("range");

		if (p != nullptr &&
		    check_if_range(request_headers.Get("if-range"), fd, st))
			file_request.range.ParseRangeHeader(p);
	}

	if (!IsTransformationEnabled()) {
		const char *p = request_headers.Get("if-match");
		if (p != nullptr && !CheckETagList(p, fd, st)) {
			DispatchError(HTTP_STATUS_PRECONDITION_FAILED,
				      {}, nullptr);
			return false;
		}

		p = request_headers.Get("if-none-match");
		if (p != nullptr) {
			if (CheckETagList(p, fd, st)) {
				DispatchNotModified(*this, tr, fd, st);
				return false;
			}

			/* RFC 2616 14.26: "If none of the entity tags match, then
			   the server MAY perform the requested method as if the
			   If-None-Match header field did not exist, but MUST also
			   ignore any If-Modified-Since header field(s) in the
			   request." */
			ignore_if_modified_since = true;
		}
	}

	if (!IsProcessorEnabled()) {
		const char *p = ignore_if_modified_since
			? nullptr
			: request_headers.Get("if-modified-since");
		if (p != nullptr) {
			const auto t = http_date_parse(p);
			if (t != std::chrono::system_clock::from_time_t(-1) &&
			    std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec) <= t) {
				DispatchNotModified(*this, tr, fd, st);
				return false;
			}
		}

		p = request_headers.Get("if-unmodified-since");
		if (p != nullptr) {
			const auto t = http_date_parse(p);
			if (t != std::chrono::system_clock::from_time_t(-1) &&
			    std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec) > t) {
				DispatchError(HTTP_STATUS_PRECONDITION_FAILED,
					      {}, nullptr);
				return false;
			}
		}
	}

	return true;
}

void
file_response_headers(GrowingBuffer &headers,
		      const ClockCache<std::chrono::system_clock> &system_clock,
		      const char *override_content_type,
		      FileDescriptor fd, const struct statx &st,
		      std::chrono::seconds expires_relative,
		      bool processor_first)
{
	if (!processor_first)
		file_cache_headers(headers, system_clock,
				   fd, st, expires_relative);

	if (override_content_type != nullptr) {
		/* content type override from the translation server */
		header_write(headers, "content-type", override_content_type);
	} else {
		char content_type[256];
		if (load_xattr_content_type(content_type, sizeof(content_type), fd)) {
			header_write(headers, "content-type", content_type);
		} else {
			header_write(headers, "content-type", "application/octet-stream");
		}
	}
}
