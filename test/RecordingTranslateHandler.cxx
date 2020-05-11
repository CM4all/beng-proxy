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
RecordingTranslateHandler::OnTranslateResponse(TranslateResponse &_response) noexcept
{
	assert(!finished);

	AllocatorPtr alloc(pool);

	response = alloc.New<TranslateResponse>();
	response->CopyFrom(alloc, _response);
	response->address.CopyFrom(alloc, _response.address);
	finished = true;
}

void
RecordingTranslateHandler::OnTranslateError(std::exception_ptr _error) noexcept
{
	assert(!finished);

	error = std::move(_error);
	finished = true;
}
