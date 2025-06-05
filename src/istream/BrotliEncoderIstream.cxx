// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "BrotliEncoderIstream.hxx"
#include "ThreadIstream.hxx"
#include "SimpleThreadIstreamFilter.hxx"
#include "UnusedPtr.hxx"

#include <brotli/encode.h>

#include <cassert>
#include <stdexcept>

class BrotliEncoderFilter final : public SimpleThreadIstreamFilter {
	BrotliEncoderState *state = nullptr;

	const BrotliEncoderMode mode;

	BrotliEncoderOperation operation = BROTLI_OPERATION_PROCESS;

public:
	explicit BrotliEncoderFilter(BrotliEncoderParams params) noexcept
		:mode(params.text_mode ? BROTLI_MODE_TEXT : BROTLI_MODE_GENERIC)
	{
	}

	~BrotliEncoderFilter() noexcept override {
		if (state != nullptr)
			BrotliEncoderDestroyInstance(state);
	}

protected:
	void CreateEncoder() noexcept;

	/* virtual methods from class SimpleThreadIstreamFilter */
	Result SimpleRun(SliceFifoBuffer &input, SliceFifoBuffer &output,
			 Params params) override;
};

inline void
BrotliEncoderFilter::CreateEncoder() noexcept
{
	assert(state == nullptr);

	state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);

	/* use medium quality; doesn't use too much CPU, but
	   compresses reasonably well */
	BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY,
				  (BROTLI_MIN_QUALITY + BROTLI_MAX_QUALITY) / 2);

	BrotliEncoderSetParameter(state, BROTLI_PARAM_MODE, mode);
}

SimpleThreadIstreamFilter::Result
BrotliEncoderFilter::SimpleRun(SliceFifoBuffer &input, SliceFifoBuffer &output,
			       Params params)
{
	if (state == nullptr)
		CreateEncoder();

	if (params.finish)
		operation = BROTLI_OPERATION_FINISH;

	const auto r = input.Read();
	const auto w = output.Write();

	std::size_t available_in = r.size();
	const uint8_t *next_in = reinterpret_cast<const uint8_t *>(r.data());

	std::size_t available_out = w.size();
	uint8_t *next_out = reinterpret_cast<uint8_t *>(w.data());

	if (!BrotliEncoderCompressStream(state, operation,
					 &available_in, &next_in,
					 &available_out, &next_out,
					 nullptr))
		throw std::runtime_error{"Brotli error"};

	input.Consume(reinterpret_cast<const std::byte *>(next_in) - r.data());
	output.Append(reinterpret_cast<std::byte *>(next_out) - w.data());

	return {
		.drained = params.finish && BrotliEncoderIsFinished(state),
	};
}

UnusedIstreamPtr
NewBrotliEncoderIstream(struct pool &pool, ThreadQueue &queue,
			UnusedIstreamPtr input,
			BrotliEncoderParams params) noexcept
{
	return NewThreadIstream(pool, queue, std::move(input),
				std::make_unique<BrotliEncoderFilter>(params));
}
