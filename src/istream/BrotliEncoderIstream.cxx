// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "BrotliEncoderIstream.hxx"
#include "UnusedPtr.hxx"
#include "New.hxx"
#include "FacadeIstream.hxx"
#include "util/DestructObserver.hxx"

#include <brotli/encode.h>

#include <cassert>
#include <stdexcept>

#include <limits.h> // for INT_MAX

static std::span<const std::byte>
BrotliEncoderTakeOutput(BrotliEncoderState *state) noexcept
{
	std::size_t size;
	const uint8_t *data = BrotliEncoderTakeOutput(state, &size);
	return {reinterpret_cast<const std::byte *>(data), size};
}

class BrotliEncoderIstream final : public FacadeIstream, DestructAnchor {
	BrotliEncoderState *state = nullptr;

	/**
	 * Pending output data from the encoder.  Since this buffer
	 * will be invalidated by the next encoder call, we need to
	 * submit it to our #IstreamHandler before feeding more data
	 * into the encoder.
	 */
	std::span<const std::byte> pending{};

	/**
	 * Do we expect to get data from the encoder?  That is, did we
	 * feed data into it without getting anything back yet?  This
	 * is used to decide whether to flush.
	 */
	bool expected = false;

	bool had_input, had_output;

public:
	BrotliEncoderIstream(struct pool &_pool, UnusedIstreamPtr _input) noexcept
		:FacadeIstream(_pool, std::move(_input))
	{
	}

	~BrotliEncoderIstream() noexcept override {
		if (state != nullptr)
			BrotliEncoderDestroyInstance(state);
	}

private:
	enum class WriteResult {
		EMPTY,
		BLOCKING,
		CONSUMED_SOME,
		CONSUMED_ALL,
		CLOSED,
	};

	/**
	 * Submit data from #pending to our #IstreamHandler.
	 */
	WriteResult SubmitPending(const DestructObserver &destructed) noexcept;

	/**
	 * Submit data from the buffer to our #IstreamHandler.
	 */
	WriteResult SubmitEncoded() noexcept;

	/**
	 * Read from our input until we have submitted some bytes to our
	 * istream handler.
	 */
	void ForceRead() noexcept;

protected:
	void CreateEncoder() noexcept;

	/* virtual methods from class Istream */
	void _Read() noexcept override;

	/* virtual methods from class IstreamHandler */
	std::size_t OnData(std::span<const std::byte> src) noexcept override;
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};

void
BrotliEncoderIstream::CreateEncoder() noexcept
{
	assert(state == nullptr);

	state = BrotliEncoderCreateInstance(nullptr, nullptr, nullptr);

	/* use medium quality; doesn't use too much CPU, but
	   compresses reasonably well */
	BrotliEncoderSetParameter(state, BROTLI_PARAM_QUALITY,
				  (BROTLI_MIN_QUALITY + BROTLI_MAX_QUALITY) / 2);
}

inline BrotliEncoderIstream::WriteResult
BrotliEncoderIstream::SubmitPending(const DestructObserver &destructed) noexcept
{
	if (pending.empty())
		return WriteResult::EMPTY;

	had_output = true;

	size_t consumed = InvokeData(pending);
	if (destructed) {
		assert(consumed == 0);
		return WriteResult::CLOSED;
	}

	if (consumed == 0)
		return WriteResult::BLOCKING;

	pending = pending.subspan(consumed);
	return pending.empty() ? WriteResult::CONSUMED_ALL : WriteResult::CONSUMED_SOME;
}

BrotliEncoderIstream::WriteResult
BrotliEncoderIstream::SubmitEncoded() noexcept
{
	assert(state != nullptr);

	const DestructObserver destructed{*this};

	bool consumed_some = false;

	while (true) {
		switch (const auto r = SubmitPending(destructed)) {
		case WriteResult::EMPTY:
			break;

		case WriteResult::CONSUMED_ALL:
			consumed_some = true;
			break;

		case WriteResult::BLOCKING:
			return consumed_some
				? WriteResult::CONSUMED_SOME
				: WriteResult::BLOCKING;

		case WriteResult::CONSUMED_SOME:
		case WriteResult::CLOSED:
			return r;
		}

		pending = BrotliEncoderTakeOutput(state);
		if (pending.empty()) {
			if (HasInput())
				return consumed_some
					? WriteResult::CONSUMED_ALL
					: WriteResult::EMPTY;

			size_t available_in = 0, available_out = 0;
			const uint8_t *next_in = nullptr;
			uint8_t *next_out = nullptr;

			if (!BrotliEncoderCompressStream(state, BROTLI_OPERATION_FINISH,
							 &available_in, &next_in,
							 &available_out, &next_out,
							 nullptr)) {
				DestroyError(std::make_exception_ptr(std::runtime_error{"Brotli finish error"}));
				return WriteResult::CLOSED;
			}

			pending = BrotliEncoderTakeOutput(state);
			if (pending.empty()) {
				DestroyEof();
				return WriteResult::CLOSED;
			}
		}

		expected = false;
	}
}

inline void
BrotliEncoderIstream::ForceRead() noexcept
{
	assert(HasInput());

	const DestructObserver destructed{*this};

	had_output = false;

	do {
		had_input = false;
		input.Read();
		if (destructed)
			return;
	} while (HasInput() && had_input && !had_output);

	if (HasInput() && !had_output && expected) {
		/* we didn't get any encoded data, even though the
		   encoder got raw data - to obey the Istream API,
		   flush the encoder */
		// TODO can we optimize this away?

		size_t available_in = 0, available_out = 0;
		const uint8_t *next_in = nullptr;
		uint8_t *next_out = nullptr;

		if (!BrotliEncoderCompressStream(state, BROTLI_OPERATION_FLUSH,
						 &available_in, &next_in,
						 &available_out, &next_out,
						 nullptr)) {
			DestroyError(std::make_exception_ptr(std::runtime_error{"Brotli flush error"}));
			return;
		}

		SubmitEncoded();
	}
}

void
BrotliEncoderIstream::_Read() noexcept
{
	if (state != nullptr) {
		switch (SubmitEncoded()) {
		case WriteResult::EMPTY:
		case WriteResult::CONSUMED_ALL:
			/* the libbrotli output buffer is empty and is
			   ready for the next
			   BrotliEncoderCompressStream() call, so
			   let's obtain data from our input */
			assert(!BrotliEncoderHasMoreOutput(state));
			assert(pending.empty());
			assert(HasInput());
			break;

		case WriteResult::BLOCKING:
		case WriteResult::CONSUMED_SOME:
			/* our handler did not consume everything */
			assert(!pending.empty());
			return;

		case WriteResult::CLOSED:
			/* bail out without touching anything */
			return;
		}
	}

	ForceRead();
}

std::size_t
BrotliEncoderIstream::OnData(const std::span<const std::byte> src) noexcept
{
	assert(HasInput());

	had_input = true;

	if (state == nullptr)
		CreateEncoder();

	size_t available_in = src.size();
	const uint8_t *next_in = reinterpret_cast<const uint8_t *>(src.data());

	/* for the first iteration, pretend the encoder consumed some
	   input data */
	bool has_consumed_input = true;

	while (true) {
		bool has_consumed_output;

		/* first submit pending data because the
		   BrotliEncoderCompressStream() call below would
		   invalidat the #pending buffer */
		switch (SubmitEncoded()) {
		case WriteResult::EMPTY:
			has_consumed_output = false;
			break;

		case WriteResult::CONSUMED_ALL:
			has_consumed_output = true;
			break;

		case WriteResult::BLOCKING:
		case WriteResult::CONSUMED_SOME:
			return src.size() - available_in;

		case WriteResult::CLOSED:
			return 0;
		}

		if (available_in == 0 ||
		    (!has_consumed_input && !has_consumed_output))
			return src.size() - available_in;

		const size_t old_available_in = available_in;

		size_t available_out = 0;
		uint8_t *next_out = nullptr;

		if (!BrotliEncoderCompressStream(state, BROTLI_OPERATION_PROCESS,
						 &available_in, &next_in,
						 &available_out, &next_out,
						 nullptr)) {
			DestroyError(std::make_exception_ptr(std::runtime_error{"Brotli error"}));
			return 0;
		}

		has_consumed_input = available_in != old_available_in;

		expected = true;
	}

	return src.size() - available_in;
}

void
BrotliEncoderIstream::OnEof() noexcept
{
	ClearInput();

	if (state == nullptr)
		CreateEncoder();

	SubmitEncoded();
}

void
BrotliEncoderIstream::OnError(std::exception_ptr ep) noexcept
{
	ClearInput();

	DestroyError(std::move(ep));
}

UnusedIstreamPtr
NewBrotliEncoderIstream(struct pool &pool, UnusedIstreamPtr input) noexcept
{
	return NewIstreamPtr<BrotliEncoderIstream>(pool, std::move(input));

}
