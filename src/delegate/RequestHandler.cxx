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

#include "bp/Request.hxx"
#include "Address.hxx"
#include "Glue.hxx"
#include "bp/FileHeaders.hxx"
#include "bp/GenerateResponse.hxx"
#include "bp/Instance.hxx"
#include "http/IncomingRequest.hxx"

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
		DispatchError(HTTP_STATUS_INTERNAL_SERVER_ERROR,
			      "Internal server error");
		return;
	}

	if (!S_ISREG(st.stx_mode)) {
		DispatchError(HTTP_STATUS_NOT_FOUND, "Not a regular file");
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

	if (request.method != HTTP_METHOD_HEAD &&
	    request.method != HTTP_METHOD_GET &&
	    !processor_focus) {
		method_not_allowed(*this, "GET, HEAD");
		return;
	}

	/* run the delegate helper */

	handler.delegate.path = path;

	delegate_stock_open(instance.delegate_stock, *request.pool,
			    address.delegate, address.child_options,
			    path,
			    *this, cancel_ptr);
}
