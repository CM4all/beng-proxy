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

inline void
Request::SubmitDirectoryIndex(const TranslateResponse &response) noexcept
{
	if (++translate.n_directory_index > 4) {
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Got too many consecutive DIRECTORY_INDEX packets",
				 1);
		return;
	}

	translate.request.directory_index = response.directory_index;
	SubmitTranslateRequest();
}

inline void
Request::OnDirectoryIndexStat(const struct statx &st) noexcept
{
	assert(translate.pending_response);

	const auto &response = *translate.pending_response;

	if (S_ISDIR(st.stx_mode))
		SubmitDirectoryIndex(response);
	else
		OnTranslateResponseAfterDirectoryIndex(std::move(translate.pending_response));
}

inline void
Request::OnDirectoryIndexStatError([[maybe_unused]] int error) noexcept
{
	assert(translate.pending_response);

	OnTranslateResponseAfterDirectoryIndex(std::move(translate.pending_response));
}

inline void
Request::CheckDirectoryIndex(UniquePoolPtr<TranslateResponse> _response, FileAt file) noexcept
{
	assert(_response);

	translate.pending_response = std::move(_response);

	instance.uring.Stat(file, AT_STATX_DONT_SYNC, STATX_TYPE,
			    BIND_THIS_METHOD(OnDirectoryIndexStat),
			    BIND_THIS_METHOD(OnDirectoryIndexStatError),
			    cancel_ptr);
}

inline void
Request::CheckDirectoryIndex(UniquePoolPtr<TranslateResponse> _response, FileDescriptor base) noexcept
{
	assert(_response);

	const auto &response = *_response;

	const char *path = response.test_path;
	assert(path != nullptr);

	CheckDirectoryIndex(std::move(_response), {base, StripBase(path)});
}

void
Request::OnDirectoryIndexBaseOpen(FileDescriptor fd) noexcept
{
	assert(translate.pending_response);

	CheckDirectoryIndex(std::move(translate.pending_response), fd);
}

void
Request::CheckDirectoryIndex(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	assert(_response);

	const auto &response = *_response;

	assert(response.directory_index.data() != nullptr);

	translate.pending_response = std::move(_response);

	if (response.test_path != nullptr)
		OpenBase(response, &Request::OnDirectoryIndexBaseOpen);
	else if (response.address.type == ResourceAddress::Type::LOCAL)
		StatFileAddress(response.address.GetFile(),
				&Request::OnDirectoryIndexStat,
				&Request::OnDirectoryIndexStatError);
	else
		LogDispatchError(HttpStatus::BAD_GATEWAY,
				 "Resource address not compatible with DIRECTORY_INDEX",
				 1);
}
