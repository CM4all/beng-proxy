// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "translation/Response.hxx"
#include "file/Address.hxx"
#include "cgi/Address.hxx"
#include "http/local/Address.hxx"
#include "io/StatAt.hxx"

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

[[gnu::pure]]
static bool
IsEnoent(const char *base, const char *path) noexcept
{
	struct statx st;
	return !StatAt(base, path,
		       AT_SYMLINK_NOFOLLOW|AT_STATX_DONT_SYNC,
		       STATX_TYPE, &st) &&
		errno == ENOENT;
}

[[gnu::pure]]
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

[[gnu::pure]]
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
	assert(response.file_not_found.data() != nullptr);

	const char *path = get_file_path(response);
	if (path == nullptr) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Resource address not compatible with TRANSLATE_FILE_NOT_FOUND",
				 1);
		return false;
	}

	if (!IsEnoent(get_file_base(response), path))
		return true;

	if (++translate.n_file_not_found > 20) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "got too many consecutive FILE_NOT_FOUND packets",
				 1);
		return false;
	}

	translate.request.file_not_found = response.file_not_found;
	SubmitTranslateRequest();
	return false;
}
