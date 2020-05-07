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

#include "Request.hxx"
#include "translation/Response.hxx"
#include "file_address.hxx"
#include "cgi/Address.hxx"
#include "lhttp_address.hxx"
#include "io/StatAt.hxx"

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

gcc_pure
static bool
IsEnoent(const char *base, const char *path) noexcept
{
	struct statx st;
	return !StatAt(base, path,
		       AT_SYMLINK_NOFOLLOW|AT_STATX_DONT_SYNC,
		       STATX_TYPE, &st) &&
		errno == ENOENT;
}

gcc_pure
static const char *
get_file_path(const TranslateResponse &response)
{
	if (response.test_path != nullptr)
		return response.test_path;

	const auto &address = response.address;
	switch (address.type) {
	case ResourceAddress::Type::NONE:
	case ResourceAddress::Type::HTTP:
	case ResourceAddress::Type::PIPE:
	case ResourceAddress::Type::NFS:
		return nullptr;

	case ResourceAddress::Type::CGI:
	case ResourceAddress::Type::FASTCGI:
	case ResourceAddress::Type::WAS:
		return address.GetCgi().path;

	case ResourceAddress::Type::LHTTP:
		return address.GetLhttp().path;

	case ResourceAddress::Type::LOCAL:
		return address.GetFile().path;

		// TODO: implement NFS
	}

	assert(false);
	gcc_unreachable();
}

gcc_pure
static const char *
get_file_base(const TranslateResponse &response) noexcept
{
	if (response.test_path != nullptr)
		return nullptr;

	const auto &address = response.address;
	switch (address.type) {
	case ResourceAddress::Type::NONE:
	case ResourceAddress::Type::HTTP:
	case ResourceAddress::Type::PIPE:
	case ResourceAddress::Type::NFS:
	case ResourceAddress::Type::CGI:
	case ResourceAddress::Type::FASTCGI:
	case ResourceAddress::Type::WAS:
	case ResourceAddress::Type::LHTTP:
		return nullptr;

	case ResourceAddress::Type::LOCAL:
		return address.GetFile().base;
	}

	assert(false);
	gcc_unreachable();
}

bool
Request::CheckFileNotFound(const TranslateResponse &response) noexcept
{
	assert(!response.file_not_found.IsNull());

	const char *path = get_file_path(response);
	if (path == nullptr) {
		LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
				 "Resource address not compatible with TRANSLATE_FILE_NOT_FOUND",
				 1);
		return false;
	}

	if (!IsEnoent(get_file_base(response), path))
		return true;

	if (++translate.n_file_not_found > 20) {
		LogDispatchError(HTTP_STATUS_BAD_GATEWAY,
				 "got too many consecutive FILE_NOT_FOUND packets",
				 1);
		return false;
	}

	translate.request.file_not_found = response.file_not_found;
	SubmitTranslateRequest();
	return false;
}
