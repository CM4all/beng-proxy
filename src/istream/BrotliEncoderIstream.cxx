// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "BrotliEncoderIstream.hxx"
#include "ThreadIstream.hxx"
#include "UnusedPtr.hxx"

#include <brotli/encode.h>

#include <cassert>
#include <stdexcept>

class BrotliEncoderFilter final : public ThreadIstreamFilter {
	BrotliEncoderState *state = nullptr;

	SliceFifoBuffer input, output;

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

	/* virtual methods from class ThreadIstreamFilter */
	void Run(ThreadIstreamInternal &i) override;
	void PostRun(ThreadIstreamInternal &i) noexcept override;
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

void
BrotliEncoderFilter::Run(ThreadIstreamInternal &i)
{
	using std::swap;

	if (state == nullptr)
		CreateEncoder();

	bool has_more_input;

	{
		const std::scoped_lock lock{i.mutex};
		input.MoveFromAllowBothNull(i.input);

		has_more_input = !i.input.empty();
		if (!i.has_input && i.input.empty())
			operation = BROTLI_OPERATION_FINISH;

		if (!output.IsNull())
			i.output.MoveFromAllowNull(output);
		else if (i.output.empty())
			swap(output, i.output);
	}

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

	const std::size_t input_consumed = reinterpret_cast<const std::byte *>(next_in) - r.data();
	input.Consume(input_consumed);
	output.Append(reinterpret_cast<std::byte *>(next_out) - w.data());

	if (available_out == 0 || (input_consumed > 0 && has_more_input))
		/* run again if:
		   1. our output buffer is full (ThreadIstream will
		      provide a new one)
		   2. there is more input in ThreadIstreamInternal but
		      in this run, there was not enough space in our
		      input buffer, but there is now
		*/
		i.again = true;

	{
		const std::scoped_lock lock{i.mutex};
		i.output.MoveFromAllowSrcNull(output);
		i.drained = output.empty();
	}
}

void
BrotliEncoderFilter::PostRun(ThreadIstreamInternal &) noexcept
{
	input.FreeIfEmpty();
	output.FreeIfEmpty();
}

UnusedIstreamPtr
NewBrotliEncoderIstream(struct pool &pool, ThreadQueue &queue,
			UnusedIstreamPtr input,
			BrotliEncoderParams params) noexcept
{
	return NewThreadIstream(pool, queue, std::move(input),
				std::make_unique<BrotliEncoderFilter>(params));
}
