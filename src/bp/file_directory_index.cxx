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

#include "file_directory_index.hxx"
#include "Request.hxx"
#include "translation/Response.hxx"
#include "file_address.hxx"
#include "io/StatAt.hxx"

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

gcc_pure
static bool
IsDirectory(const char *base, const char *path) noexcept
{
	struct statx st;
	return StatAt(base, path, AT_STATX_DONT_SYNC, STATX_TYPE, &st) &&
		S_ISDIR(st.stx_mode);
}

gcc_pure
static bool
IsDirectory(const FileAddress &address) noexcept
{
	return IsDirectory(address.base, address.path);
}

bool
check_directory_index(Request &request,
		      const TranslateResponse &response)
{
	assert(!response.directory_index.IsNull());

	if (response.test_path != nullptr) {
		if (!IsDirectory(nullptr, response.test_path))
			return true;
	} else {
		switch (response.address.type) {
		case ResourceAddress::Type::NONE:
		case ResourceAddress::Type::HTTP:
		case ResourceAddress::Type::LHTTP:
		case ResourceAddress::Type::PIPE:
		case ResourceAddress::Type::CGI:
		case ResourceAddress::Type::FASTCGI:
		case ResourceAddress::Type::WAS:
		case ResourceAddress::Type::NFS:
			request.LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
						 "Resource address not compatible with DIRECTORY_INDEX",
						 1);
			return false;

		case ResourceAddress::Type::LOCAL:
			if (!IsDirectory(response.address.GetFile()))
				return true;

			break;

			// TODO: implement NFS
		}
	}

	if (++request.translate.n_directory_index > 4) {
		request.LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
					 "Got too many consecutive DIRECTORY_INDEX packets",
					 1);
		return false;
	}

	request.translate.request.directory_index = response.directory_index;
	request.SubmitTranslateRequest();
	return false;
}
