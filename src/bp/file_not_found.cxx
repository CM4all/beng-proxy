// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "Request.hxx"
#include "Instance.hxx"
#include "translation/Response.hxx"
#include "io/FileAt.hxx"

#include <assert.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <errno.h>

inline void
Request::SubmitFileNotFound(const TranslateResponse &response) noexcept
{
	if (++translate.n_file_not_found > 20) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Got too many consecutive DIRECTORY_INDEX packets",
				 1);
		return;
	}

	translate.request.file_not_found = response.file_not_found;
	SubmitTranslateRequest();
}

inline void
Request::OnFileNotFoundStat([[maybe_unused]] const struct statx &st) noexcept
{
	assert(translate.pending_response);

	OnTranslateResponseAfterFileNotFound(std::move(translate.pending_response));
}

inline void
Request::OnFileNotFoundStatError(int error) noexcept
{
	assert(translate.pending_response);

	if (error == ENOENT)
		SubmitFileNotFound(*translate.pending_response);
	else
		OnTranslateResponseAfterFileNotFound(std::move(translate.pending_response));
}

void
Request::CheckFileNotFound(UniquePoolPtr<TranslateResponse> _response, FileAt file) noexcept
{
	assert(_response);

	translate.pending_response = std::move(_response);

	instance.uring.Stat(file, AT_STATX_DONT_SYNC, STATX_TYPE,
			    BIND_THIS_METHOD(OnFileNotFoundStat),
			    BIND_THIS_METHOD(OnFileNotFoundStatError),
			    cancel_ptr);
}

[[gnu::pure]]
static const char *
get_file_path(const TranslateResponse &response)
{
	if (response.test_path != nullptr)
		return response.test_path;

	return response.address.GetFileOrExecutablePath();
}

inline void
Request::CheckFileNotFound(UniquePoolPtr<TranslateResponse> _response, FileDescriptor base) noexcept
{
	assert(_response);

	const auto &response = *_response;

	const char *path = get_file_path(response);
	if (path == nullptr) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Resource address not compatible with TRANSLATE_FILE_NOT_FOUND",
				 1);
		return;
	}

	CheckFileNotFound(std::move(_response), {base, StripBase(path)});
}

void
Request::OnFileNotFoundBaseOpen(FileDescriptor fd) noexcept
{
	assert(translate.pending_response);

	CheckFileNotFound(std::move(translate.pending_response), fd);
}

void
Request::CheckFileNotFound(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	assert(_response);

	const auto &response = *_response;

	assert(response.file_not_found.data() != nullptr);

	translate.pending_response = std::move(_response);
	OpenBase(response, &Request::OnFileNotFoundBaseOpen);
}
