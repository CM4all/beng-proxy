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

#pragma once

#include <exception>

#include <stddef.h>

struct pool;
class UnusedIstreamPtr;
class Rubber;
class RubberAllocation;
class RubberSink;
class CancellablePointer;

class RubberSinkHandler {
public:
	virtual void RubberDone(RubberAllocation &&a, size_t size) noexcept = 0;
	virtual void RubberOutOfMemory() noexcept = 0;
	virtual void RubberTooLarge() noexcept = 0;
	virtual void RubberError(std::exception_ptr ep) noexcept = 0;
};

/**
 * An istream sink that copies data into a rubber allocation.
 */
RubberSink *
sink_rubber_new(struct pool &pool, UnusedIstreamPtr input,
		Rubber &rubber, size_t max_size,
		RubberSinkHandler &handler,
		CancellablePointer &cancel_ptr) noexcept;

void
sink_rubber_read(RubberSink &sink) noexcept;
