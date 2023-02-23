// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "translation/Response.hxx"
#include "file/Address.hxx"
#include "cgi/Address.hxx"
#include "http/local/Address.hxx"
#include "http/IncomingRequest.hxx"
#include "pool/pool.hxx"
#include "io/StatAt.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

[[gnu::pure]]
static bool
IsEnotdir(const char *base, const char *path) noexcept
{
	struct statx st;
	return !StatAt(base, path, AT_STATX_DONT_SYNC, STATX_TYPE, &st) &&
		errno == ENOTDIR;
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

inline bool
Request::SubmitEnotdir(const TranslateResponse &response) noexcept
{
	translate.request.enotdir = response.enotdir;

	const char *const uri = request.uri;
	if (translate.enotdir_uri == nullptr) {
		translate.request.uri = translate.enotdir_uri =
			p_strdup(&pool, uri);
		translate.enotdir_path_info = uri + strlen(uri);
	}

	const char *slash = (const char *)
		memrchr(uri, '/', translate.enotdir_path_info - uri);
	if (slash == nullptr || slash == uri)
		return true;

	translate.enotdir_uri[slash - uri] = 0;
	translate.enotdir_path_info = slash;

	SubmitTranslateRequest();
	return false;
}

bool
Request::CheckFileEnotdir(const TranslateResponse &response) noexcept
{
	assert(response.enotdir.data() != nullptr);

	const char *path = get_file_path(response);
	if (path == nullptr) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Resource address not compatible with ENOTDIR",
				 1);
		return false;
	}

	if (IsEnotdir(get_file_base(response), path))
		return SubmitEnotdir(response);

	return true;
}

void
Request::ApplyFileEnotdir() noexcept
{
	if (translate.enotdir_path_info != nullptr) {
		/* append the path_info to the resource address */

		auto address =
			translate.address.Apply(pool, translate.enotdir_path_info);
		if (address.IsDefined())
			translate.address = std::move(address);
	}
}
