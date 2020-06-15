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

#include "IstreamDataSource.hxx"

#include <string.h>

namespace NgHttp2 {

ssize_t
IstreamDataSource::ReadCallback(uint8_t *buf, size_t length,
				uint32_t &data_flags) noexcept
{
	if (error) {
		// TODO
		return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
	}

	auto &buffer = sink.GetBuffer();
	auto r = buffer.Read();
	if (r.empty()) {
		if (eof) {
			data_flags |= NGHTTP2_DATA_FLAG_EOF;
			return 0;
		} else {
			sink.Read();
			if (error) {
				// TODO
				return NGHTTP2_ERR_TEMPORAL_CALLBACK_FAILURE;
			}

			r = buffer.Read();
			if (r.empty()) {
				if (eof) {
					data_flags |= NGHTTP2_DATA_FLAG_EOF;
					return 0;
				}

				return NGHTTP2_ERR_DEFERRED;
			}
		}
	}

	size_t nbytes = std::min(r.size, length);
	memcpy(buf, r.data, nbytes);
	buffer.Consume(nbytes);

	if (buffer.empty()) {
		buffer.Free();
		if (eof)
			data_flags |= NGHTTP2_DATA_FLAG_EOF;
	}

	return nbytes;
}

} // namespace NgHttp2
