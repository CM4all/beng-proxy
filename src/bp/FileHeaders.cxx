// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "FileHeaders.hxx"
#include "Request.hxx"
#include "Instance.hxx"
#include "file/Headers.hxx"
#include "http/CommonHeaders.hxx"
#include "http/HeaderWriter.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Headers.hxx"
#include "translation/Vary.hxx"
#include "http/List.hxx"
#include "http/Date.hxx"
#include "http/Method.hxx"
#include "event/Loop.hxx"
#include "io/FileDescriptor.hxx"
#include "util/NumberParser.hxx"
#include "util/StringAPI.hxx"

#include <assert.h>
#include <sys/stat.h>
#include <sys/xattr.h>

using std::string_view_literals::operator""sv;

[[gnu::pure]]
static std::chrono::seconds
read_xattr_max_age(FileDescriptor fd) noexcept
{
	assert(fd.IsDefined());

	char buffer[32];
	ssize_t nbytes = fgetxattr(fd.Get(), "user.MaxAge",
				   buffer, sizeof(buffer));
	if (nbytes <= 0)
		return std::chrono::seconds::zero();

	unsigned max_age;
	if (!ParseIntegerTo({buffer, static_cast<std::size_t>(nbytes)}, max_age))
		return std::chrono::seconds::zero();

	return std::chrono::seconds(max_age);
}

static void
generate_expires(GrowingBuffer &headers,
		 std::chrono::system_clock::time_point now,
		 std::chrono::system_clock::duration max_age) noexcept
{
	constexpr std::chrono::system_clock::duration max_max_age =
		std::chrono::hours(365 * 24);
	if (max_age > max_max_age)
		/* limit max_age to approximately one year */
		max_age = max_max_age;

	/* generate an "Expires" response header */
	header_write(headers, "expires"sv, now + max_age);
}

[[gnu::pure]]
static bool
CheckETagList(const char *list, FileDescriptor fd,
	      const struct statx &st,
	      bool use_xattr) noexcept
{
	assert(list != nullptr);

	if (StringIsEqual(list, "*"))
		return true;

	char buffer[256];
	GetAnyETag(buffer, sizeof(buffer), fd, st, use_xattr);
	return http_list_contains(list, buffer);
}

static void
MakeETag(GrowingBuffer &headers, FileDescriptor fd, const struct statx &st,
	bool use_xattr) noexcept
{
	char buffer[512];
	GetAnyETag(buffer, sizeof(buffer), fd, st, use_xattr);

	header_write(headers, "etag", buffer);
}

static void
file_cache_headers(GrowingBuffer &headers,
		   const ClockCache<std::chrono::system_clock> &system_clock,
		   FileDescriptor fd, const struct statx &st,
		   std::chrono::seconds max_age,
		   bool use_xattr) noexcept
{
	header_write(headers, "last-modified"sv, std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec));

	MakeETag(headers, fd, st, use_xattr);

	if (use_xattr && max_age == std::chrono::seconds::zero() && fd.IsDefined())
		max_age = read_xattr_max_age(fd);

	if (max_age > std::chrono::seconds::zero())
		generate_expires(headers, system_clock.now(), max_age);
}

/**
 * Verifies the If-Range request header (RFC 2616 14.27).
 */
static bool
check_if_range(const char *if_range,
	       FileDescriptor fd, const struct statx &st,
	       bool use_xattr) noexcept
{
	if (if_range == nullptr)
		return true;

	const auto t = http_date_parse(if_range);
	if (t != std::chrono::system_clock::from_time_t(-1))
		return std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec) == t;

	char etag[256];
	GetAnyETag(etag, sizeof(etag), fd, st, use_xattr);
	return StringIsEqual(if_range, etag);
}

/**
 * Generate a "304 Not Modified" response.
 */
static void
DispatchNotModified(Request &request2, const TranslateResponse &tr,
		    FileDescriptor fd, const struct statx &st,
		    bool use_xattr) noexcept
{
	HttpHeaders headers;
	auto &headers2 = headers.GetBuffer();

	file_cache_headers(headers2,
			   request2.instance.event_loop.GetSystemClockCache(),
			   fd, st, tr.GetExpiresRelative(request2.HasQueryString()),
			   use_xattr);

	write_translation_vary_header(headers2, tr);

	request2.DispatchError(HttpStatus::NOT_MODIFIED,
			       std::move(headers), nullptr);
}

bool
Request::EvaluateFileRequest(FileDescriptor fd, const struct statx &st,
			     struct file_request &file_request) noexcept
{
	const auto &request_headers = request.headers;
	const auto &tr = *translate.response;
	bool ignore_if_modified_since = false;

	const bool use_xattr = instance.config.use_xattr;

	if (tr.status == HttpStatus{} && request.method == HttpMethod::GET &&
	    !IsTransformationEnabled()) {
		const char *p = request_headers.Get(range_header);

		if (p != nullptr &&
		    check_if_range(request_headers.Get(if_range_header), fd, st,
				   use_xattr))
			file_request.range.ParseRangeHeader(p);
	}

	if (!IsTransformationEnabled()) {
		const char *p = request_headers.Get(if_match_header);
		if (p != nullptr && !CheckETagList(p, fd, st, use_xattr)) {
			DispatchError(HttpStatus::PRECONDITION_FAILED,
				      {}, nullptr);
			return false;
		}

		p = request_headers.Get(if_none_match_header);
		if (p != nullptr) {
			if (CheckETagList(p, fd, st, use_xattr)) {
				DispatchNotModified(*this, tr, fd, st,
						    use_xattr);
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
			: request_headers.Get(if_modified_since_header);
		if (p != nullptr) {
			const auto t = http_date_parse(p);
			if (t != std::chrono::system_clock::from_time_t(-1) &&
			    std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec) <= t) {
				DispatchNotModified(*this, tr, fd, st, use_xattr);
				return false;
			}
		}

		p = request_headers.Get(if_unmodified_since_header);
		if (p != nullptr) {
			const auto t = http_date_parse(p);
			if (t != std::chrono::system_clock::from_time_t(-1) &&
			    std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec) > t) {
				DispatchError(HttpStatus::PRECONDITION_FAILED,
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
		      bool processor_first, bool use_xattr) noexcept
{
	if (!processor_first)
		file_cache_headers(headers, system_clock,
				   fd, st, expires_relative,
				   use_xattr);

	if (override_content_type != nullptr) {
		/* content type override from the translation server */
		header_write(headers, "content-type", override_content_type);
	} else {
		char content_type[256];
		if (use_xattr && load_xattr_content_type(content_type, sizeof(content_type), fd)) {
			header_write(headers, "content-type", content_type);
		} else {
			header_write(headers, "content-type", "application/octet-stream");
		}
	}
}
