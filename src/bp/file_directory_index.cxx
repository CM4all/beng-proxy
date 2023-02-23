// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "translation/Response.hxx"
#include "file/Address.hxx"
#include "io/StatAt.hxx"

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>

[[gnu::pure]]
static bool
IsDirectory(const char *base, const char *path) noexcept
{
	struct statx st;
	return StatAt(base, path, AT_STATX_DONT_SYNC, STATX_TYPE, &st) &&
		S_ISDIR(st.stx_mode);
}

[[gnu::pure]]
static bool
IsDirectory(const FileAddress &address) noexcept
{
	return IsDirectory(address.base, address.path);
}

bool
Request::CheckDirectoryIndex(const TranslateResponse &response) noexcept
{
	assert(response.directory_index.data() != nullptr);

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
			LogDispatchError(HttpStatus::BAD_GATEWAY,
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

	if (++translate.n_directory_index > 4) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Got too many consecutive DIRECTORY_INDEX packets",
				 1);
		return false;
	}

	translate.request.directory_index = response.directory_index;
	SubmitTranslateRequest();
	return false;
}
