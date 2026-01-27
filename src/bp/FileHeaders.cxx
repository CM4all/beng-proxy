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

using std::string_view_literals::operator""sv;

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
CheckETagList(const char *list,
	      const struct statx &st) noexcept
{
	assert(list != nullptr);

	if (StringIsEqual(list, "*"))
		return true;

	char buffer[256];
	GetAnyETag(buffer, st);
	return http_list_contains(list, buffer);
}

static void
MakeETag(GrowingBuffer &headers, const struct statx &st) noexcept
{
	char buffer[512];
	GetAnyETag(buffer, st);

	header_write(headers, "etag", buffer);
}

static void
file_cache_headers(GrowingBuffer &headers,
		   const ClockCache<std::chrono::system_clock> &system_clock,
		   const struct statx &st,
		   std::chrono::seconds max_age) noexcept
{
	header_write(headers, "last-modified"sv, std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec));

	MakeETag(headers, st);

	if (max_age > std::chrono::seconds::zero())
		generate_expires(headers, system_clock.now(), max_age);
}

/**
 * Verifies the If-Range request header (RFC 2616 14.27).
 */
static bool
check_if_range(const char *if_range, const struct statx &st) noexcept
{
	if (if_range == nullptr)
		return true;

	const auto t = http_date_parse(if_range);
	if (t != std::chrono::system_clock::from_time_t(-1))
		return std::chrono::system_clock::from_time_t(st.stx_mtime.tv_sec) == t;

	char etag[256];
	GetAnyETag(etag, st);
	return StringIsEqual(if_range, etag);
}

/**
 * Generate a "304 Not Modified" response.
 */
static void
DispatchNotModified(Request &request2, const TranslateResponse &tr,
		    const struct statx &st) noexcept
{
	HttpHeaders headers;
	auto &headers2 = headers.GetBuffer();

	file_cache_headers(headers2,
			   request2.instance.event_loop.GetSystemClockCache(),
			   st, tr.GetExpiresRelative(request2.HasQueryString()));

	write_translation_vary_header(headers2, tr);

	request2.DispatchError(HttpStatus::NOT_MODIFIED,
			       std::move(headers), nullptr);
}

bool
Request::EvaluateFileRequest(const struct statx &st,
			     struct file_request &file_request) noexcept
{
	const auto &request_headers = request.headers;
	const auto &tr = *translate.response;
	bool ignore_if_modified_since = false;

	if (tr.status == HttpStatus{} && request.method == HttpMethod::GET &&
	    !IsTransformationEnabled()) {
		const char *p = request_headers.Get(range_header);

		if (p != nullptr &&
		    check_if_range(request_headers.Get(if_range_header), st))
			file_request.range.ParseRangeHeader(p);
	}

	if (!IsTransformationEnabled()) {
		const char *p = request_headers.Get(if_match_header);
		if (p != nullptr && !CheckETagList(p, st)) {
			DispatchError(HttpStatus::PRECONDITION_FAILED,
				      {}, nullptr);
			return false;
		}

		p = request_headers.Get(if_none_match_header);
		if (p != nullptr) {
			if (CheckETagList(p, st)) {
				DispatchNotModified(*this, tr, st);
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
				DispatchNotModified(*this, tr, st);
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
		      const char *content_type,
		      const struct statx &st,
		      std::chrono::seconds expires_relative,
		      bool processor_first) noexcept
{
	assert(content_type != nullptr);

	if (!processor_first)
		file_cache_headers(headers, system_clock,
				   st, expires_relative);

	header_write(headers, "content-type", content_type);
}
