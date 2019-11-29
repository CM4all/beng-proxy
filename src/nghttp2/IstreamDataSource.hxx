/*
 * Copyright 2007-2019 Content Management AG
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

#include "istream/FifoBufferSink.hxx"
#include "istream/UnusedPtr.hxx"

#include <nghttp2/nghttp2.h>

namespace NgHttp2 {

/**
 * Adapter between an #Istream and a #nghttp2_data_source.
 */
class IstreamDataSource final : FifoBufferSinkHandler {
	nghttp2_session *const session;
	const int32_t stream_id;

	FifoBufferSink sink;

	bool eof = false, error = false;

public:
	IstreamDataSource(nghttp2_session *_session, int32_t _stream_id,
			  UnusedIstreamPtr &&_input) noexcept
		:session(_session), stream_id(_stream_id),
		 sink(std::move(_input), *this) {}

	nghttp2_data_provider MakeDataProvider() noexcept {
		nghttp2_data_provider dp;
		dp.source.ptr = this;
		dp.read_callback = ReadCallback;
		return dp;
	}

private:
	/* virtual methods from class FifoBufferSinkHandler */
	bool OnFifoBufferSinkData() noexcept override {
		nghttp2_session_resume_data(session, stream_id);
		return true;
	}

	void OnFifoBufferSinkEof() noexcept override {
		eof = true;
		nghttp2_session_resume_data(session, stream_id);
	}

	void OnFifoBufferSinkError(std::exception_ptr ep) noexcept override {
		// TODO how to propagate the exception details?
		(void)ep;
		error = true;
		nghttp2_session_resume_data(session, stream_id);

		// TODO: use nghttp2_submit_rst_stream()?
	}

	/* libnghttp2 callbacks */
	ssize_t ReadCallback(uint8_t *buf, size_t length,
			     uint32_t &data_flags) noexcept;

	static ssize_t ReadCallback(nghttp2_session *, int32_t,
				    uint8_t *buf, size_t length,
				    uint32_t *data_flags,
				    nghttp2_data_source *source,
				    void *) noexcept {
		auto &ids = *(IstreamDataSource *)source->ptr;
		return ids.ReadCallback(buf, length, *data_flags);
	}
};

} // namespace NgHttp2
