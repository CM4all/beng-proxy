// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
			handler.OnIstreamDataSourceWaiting();
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

	size_t nbytes = std::min(r.size(), length);
	memcpy(buf, r.data(), nbytes);
	buffer.Consume(nbytes);
	transmitted += nbytes;

	if (buffer.empty()) {
		buffer.Free();
		if (eof)
			data_flags |= NGHTTP2_DATA_FLAG_EOF;
	}

	return nbytes;
}

} // namespace NgHttp2
