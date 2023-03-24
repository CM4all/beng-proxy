// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Cache.hxx"
#include "Address.hxx"
#include "translation/Vary.hxx"
#include "http/HeaderWriter.hxx"
#include "bp/FileHeaders.hxx"
#include "bp/Request.hxx"
#include "bp/Instance.hxx"
#include "http/Headers.hxx"
#include "http/IncomingRequest.hxx"
#include "http/Method.hxx"
#include "pool/pool.hxx"

#include <assert.h>
#include <sys/stat.h>

using std::string_view_literals::operator""sv;

/*
 * nfs_cache_handler
 *
 */

void
Request::OnNfsCacheResponse(NfsCacheHandle &handle,
			    const struct statx &st) noexcept
{
	const TranslateResponse &tr = *translate.response;

	struct file_request file_request(st.stx_size);
	if (!EvaluateFileRequest(FileDescriptor::Undefined(), st, file_request))
		return;

	const char *override_content_type = translate.content_type;
	if (override_content_type == nullptr)
		override_content_type = translate.address.GetNfs().content_type;

	HttpHeaders headers;
	GrowingBuffer &headers2 = headers.GetBuffer();
	header_write(headers2, "cache-control", "max-age=60");

	file_response_headers(headers2,
			      instance.event_loop.GetSystemClockCache(),
			      override_content_type,
			      FileDescriptor::Undefined(), st,
			      tr.GetExpiresRelative(HasQueryString()),
			      IsProcessorFirst(),
			      false);
	write_translation_vary_header(headers2, tr);

	auto status = tr.status == HttpStatus{} ? HttpStatus::OK : tr.status;

	/* generate the Content-Range header */

	header_write(headers2, "accept-ranges", "bytes");

	bool no_body = false;

	switch (file_request.range.type) {
	case HttpRangeRequest::Type::NONE:
		break;

	case HttpRangeRequest::Type::VALID:
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

		no_body = true;
		break;
	}

	UnusedIstreamPtr response_body;
	if (!no_body)
		response_body = nfs_cache_handle_open(pool, handle,
						      file_request.range.skip,
						      file_request.range.size);

	DispatchResponse(status, std::move(headers),
			 std::move(response_body));
}

void
Request::OnNfsCacheError(std::exception_ptr ep) noexcept
{
	LogDispatchError(ep);
}

/*
 * public
 *
 */

void
Request::HandleNfsAddress() noexcept
{
	const auto &address = translate.address.GetNfs();
	assert(address.server != NULL);
	assert(address.export_name != NULL);
	assert(address.path != NULL);

	/* check request */

	if (request.method != HttpMethod::HEAD &&
	    request.method != HttpMethod::GET &&
	    !processor_focus) {
		DispatchMethodNotAllowed("GET, HEAD");
		return;
	}

	/* run the delegate helper */

	nfs_cache_request(pool, *instance.nfs_cache,
			  address.server, address.export_name, address.path,
			  *this, cancel_ptr);
}
