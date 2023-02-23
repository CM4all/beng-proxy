// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "bp/Request.hxx"
#include "Address.hxx"
#include "Glue.hxx"
#include "bp/FileHeaders.hxx"
#include "bp/Instance.hxx"
#include "http/Method.hxx"
#include "http/IncomingRequest.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

/*
 * delegate_handler
 *
 */

void
Request::OnDelegateSuccess(UniqueFileDescriptor fd)
{
	/* get file information */

	struct statx st;
	if (statx(fd.Get(), "", AT_EMPTY_PATH,
		  STATX_TYPE|STATX_MTIME|STATX_INO|STATX_SIZE, &st) < 0) {
		DispatchError(HttpStatus::INTERNAL_SERVER_ERROR,
			      "Internal server error");
		return;
	}

	if (!S_ISREG(st.stx_mode)) {
		DispatchError(HttpStatus::NOT_FOUND, "Not a regular file");
		return;
	}

	/* request options */

	struct file_request file_request(st.stx_size);
	if (!EvaluateFileRequest(fd, st, file_request)) {
		return;
	}

	/* build the response */

	DispatchFile(handler.delegate.path, std::move(fd), st, file_request);
}

void
Request::OnDelegateError(std::exception_ptr ep)
{
	LogDispatchError(ep);
}

/*
 * public
 *
 */

void
Request::HandleDelegateAddress(const DelegateAddress &address,
			       const char *path) noexcept
{
	assert(path != nullptr);

	/* check request */

	if (request.method != HttpMethod::HEAD &&
	    request.method != HttpMethod::GET &&
	    !processor_focus) {
		DispatchMethodNotAllowed("GET, HEAD");
		return;
	}

	/* run the delegate helper */

	handler.delegate.path = path;

	delegate_stock_open(instance.delegate_stock, *request.pool,
			    address.delegate, address.child_options,
			    path,
			    *this, cancel_ptr);
}
