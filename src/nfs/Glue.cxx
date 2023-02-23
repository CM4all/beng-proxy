// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Glue.hxx"
#include "Cache.hxx"
#include "http/ResponseHandler.hxx"
#include "file/Headers.hxx"
#include "strmap.hxx"
#include "pool/pool.hxx"
#include "istream/UnusedPtr.hxx"
#include "io/FileDescriptor.hxx"
#include "AllocatorPtr.hxx"

#include <sys/stat.h>

struct NfsRequest final : NfsCacheHandler {
	struct pool &pool;

	const char *const path;
	const char *const content_type;

	HttpResponseHandler &handler;

	NfsRequest(struct pool &_pool, const char *_path,
		   const char *_content_type,
		   HttpResponseHandler &_handler)
		:pool(_pool), path(_path), content_type(_content_type),
		 handler(_handler) {
	}

	/* virtual methods from NfsCacheHandler */
	void OnNfsCacheResponse(NfsCacheHandle &handle,
				const struct statx &st) noexcept override;

	void OnNfsCacheError(std::exception_ptr ep) noexcept override {
		handler.InvokeError(ep);
	}
};

void
NfsRequest::OnNfsCacheResponse(NfsCacheHandle &handle,
			       const struct statx &st) noexcept
{
	auto headers = static_response_headers(pool, FileDescriptor::Undefined(),
					       st, content_type);
	headers.Add(pool, "cache-control", "max-age=60");

	// TODO: handle revalidation etc.
	handler.InvokeResponse(HttpStatus::OK, std::move(headers),
			       nfs_cache_handle_open(pool, handle,
						     0, st.stx_size));
}

/*
 * constructor
 *
 */

void
nfs_request(struct pool &pool, NfsCache &nfs_cache,
	    const char *server, const char *export_name, const char *path,
	    const char *content_type,
	    HttpResponseHandler &handler,
	    CancellablePointer &cancel_ptr)
{
	auto r = NewFromPool<NfsRequest>(pool, pool, path, content_type,
					 handler);

	nfs_cache_request(pool, nfs_cache, server, export_name, path,
			  *r, cancel_ptr);
}
