// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "Instance.hxx"
#include "translation/Response.hxx"
#include "file/Address.hxx"
#include "cgi/Address.hxx"
#include "http/local/Address.hxx"
#include "http/IncomingRequest.hxx"
#include "pool/pool.hxx"
#include "io/FileAt.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>
#include <fcntl.h>
#include <string.h>
#include <sys/stat.h>
#include <errno.h>

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

inline void
Request::OnEnotdirStat([[maybe_unused]] const struct statx &st) noexcept
{
	assert(translate.pending_response);

	OnTranslateResponseAfterEnotdir(std::move(translate.pending_response));
}

inline void
Request::OnEnotdirStatError(int error) noexcept
{
	assert(translate.pending_response);

	if (error != ENOTDIR || SubmitEnotdir(*translate.pending_response))
		OnTranslateResponseAfterEnotdir(std::move(translate.pending_response));
}

void
Request::CheckFileEnotdir(UniquePoolPtr<TranslateResponse> _response, FileAt file) noexcept
{
	assert(_response);

	translate.pending_response = std::move(_response);

	instance.uring.Stat(file, AT_STATX_DONT_SYNC, STATX_TYPE,
			    BIND_THIS_METHOD(OnEnotdirStat),
			    BIND_THIS_METHOD(OnEnotdirStatError),
			    cancel_ptr);
}

inline void
Request::OnEnotdirBaseOpen(FileDescriptor fd, SharedLease lease) noexcept
{
	assert(translate.pending_response);

	const auto &response = *translate.pending_response;

	handler.file.base = fd;
	handler.file.base_lease = std::move(lease);

	CheckFileEnotdir(std::move(translate.pending_response),
			 {fd, get_file_path(response)});
}

void
Request::CheckFileEnotdir(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	assert(_response);

	const auto &response = *_response;

	assert(response.enotdir.data() != nullptr);

	const char *path = get_file_path(response);
	if (path == nullptr) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Resource address not compatible with ENOTDIR",
				 1);
		return;
	}

	if (const char *base = get_file_base(response)) {
		translate.pending_response = std::move(_response);
		instance.fd_cache.Get(base, O_PATH|O_DIRECTORY,
				      BIND_THIS_METHOD(OnEnotdirBaseOpen),
				      BIND_THIS_METHOD(OnBaseOpenError),
				      cancel_ptr);
	} else
		CheckFileEnotdir(std::move(_response),
				 {FileDescriptor::Undefined(), path});
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
