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

	return response.address.GetFileOrExecutablePath();
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

inline void
Request::CheckFileEnotdir(UniquePoolPtr<TranslateResponse> _response, FileAt file) noexcept
{
	assert(_response);

	translate.pending_response = std::move(_response);

	instance.uring.Stat(file, AT_STATX_DONT_SYNC, STATX_TYPE,
			    BIND_THIS_METHOD(OnEnotdirStat),
			    BIND_THIS_METHOD(OnEnotdirStatError),
			    cancel_ptr);
}

void
Request::OnEnotdirBaseOpen(FileDescriptor fd, [[maybe_unused]] std::string_view strip_base) noexcept
{
	assert(translate.pending_response);

	const auto &response = *translate.pending_response;

	const char *path = get_file_path(response);
	if (path == nullptr) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Resource address not compatible with ENOTDIR",
				 1);
		return;
	}

	CheckFileEnotdir(std::move(translate.pending_response),
			 {fd, StripBase(path)});
}

void
Request::CheckFileEnotdir(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	assert(_response);

	const auto &response = *_response;

	assert(response.enotdir.data() != nullptr);

	translate.pending_response = std::move(_response);

	if (response.test_path != nullptr ||
	    response.address.type != ResourceAddress::Type::LOCAL)
		OpenBase(response, &Request::OnEnotdirBaseOpen);
	else
		StatFileAddress(response.address.GetFile(),
				&Request::OnEnotdirStat,
				&Request::OnEnotdirStatError);
}

void
Request::ApplyFileEnotdir() noexcept
{
	if (translate.enotdir_path_info != nullptr) {
		/* append the path_info to the resource address */

		auto address =
			translate.address.Apply(pool, translate.enotdir_path_info);
		if (address.IsDefined()) {
			translate.address = std::move(address);
			translate.address_id = StringWithHash{nullptr};
		}
	}
}
