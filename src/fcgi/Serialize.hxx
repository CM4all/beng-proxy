/*
 * Copyright 2007-2021 CM4all GmbH
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

/*
 * Serialize and deserialize FastCGI packets.
 */

#pragma once

#include <stdint.h>
#include <stddef.h>

class GrowingBuffer;
class StringMap;
struct StringView;

class FcgiRecordSerializer {
	GrowingBuffer &buffer;
	struct fcgi_record_header *const header;

public:
	FcgiRecordSerializer(GrowingBuffer &_buffer, uint8_t type,
			     uint16_t request_id_be) noexcept;

	GrowingBuffer &GetBuffer() {
		return buffer;
	}

	void Commit(size_t content_length) noexcept;
};

class FcgiParamsSerializer {
	FcgiRecordSerializer record;

	size_t content_length = 0;

public:
	FcgiParamsSerializer(GrowingBuffer &_buffer,
			     uint16_t request_id_be) noexcept;

	FcgiParamsSerializer &operator()(StringView name,
					 StringView value) noexcept;

	void Headers(const StringMap &headers) noexcept;

	void Commit() noexcept {
		record.Commit(content_length);
	}
};
