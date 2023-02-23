// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "RecordingTranslateHandler.hxx"
#include "translation/Response.hxx"
#include "pool/pool.hxx"
#include "AllocatorPtr.hxx"

#include <assert.h>

RecordingTranslateHandler::RecordingTranslateHandler(struct pool &parent_pool) noexcept
	:pool(pool_new_libc(&parent_pool,
			    "RecordingTranslateHandler"))
{
}

void
RecordingTranslateHandler::OnTranslateResponse(UniquePoolPtr<TranslateResponse> _response) noexcept
{
	assert(!finished);

	AllocatorPtr alloc(pool);

	response = UniquePoolPtr<TranslateResponse>::Make(pool);
	response->CopyFrom(alloc, *_response);
	response->address.CopyFrom(alloc, _response->address);
	finished = true;
}

void
RecordingTranslateHandler::OnTranslateError(std::exception_ptr _error) noexcept
{
	assert(!finished);

	error = std::move(_error);
	finished = true;
}
