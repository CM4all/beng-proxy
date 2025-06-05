// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#pragma once

#include "istream/FifoBufferSink.hxx"
#include "istream/UnusedPtr.hxx"

#include <nghttp2/nghttp2.h>

namespace NgHttp2 {

class IstreamDataSourceHandler {
public:
	virtual void OnIstreamDataSourceWaiting() noexcept {}
	virtual void OnIstreamDataSourceReady() noexcept = 0;
};

/**
 * Adapter between an #Istream and a #nghttp2_data_source.
 */
class IstreamDataSource final : FifoBufferSinkHandler {
	IstreamDataSourceHandler &handler;

	FifoBufferSink sink;

	uint64_t transmitted = 0;

	bool eof = false, error = false;

public:
	IstreamDataSource(UnusedIstreamPtr &&_input,
			  IstreamDataSourceHandler &_handler) noexcept
		:handler(_handler),
		 sink(std::move(_input), *this) {}

	nghttp2_data_provider MakeDataProvider() noexcept {
		nghttp2_data_provider dp;
		dp.source.ptr = this;
		dp.read_callback = ReadCallback;
		return dp;
	}

	/**
	 * Returns the number of bytes transmitted to libnghttp2 in
	 * the read callback.
	 */
	uint64_t GetTransmitted() const noexcept {
		return transmitted;
	}

private:
	/* virtual methods from class FifoBufferSinkHandler */
	bool OnFifoBufferSinkData() noexcept override {
		handler.OnIstreamDataSourceReady();
		return true;
	}

	void OnFifoBufferSinkEof() noexcept override {
		eof = true;
		handler.OnIstreamDataSourceReady();
	}

	void OnFifoBufferSinkError(std::exception_ptr ep) noexcept override {
		// TODO how to propagate the exception details?
		(void)ep;
		error = true;

		handler.OnIstreamDataSourceReady();

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
