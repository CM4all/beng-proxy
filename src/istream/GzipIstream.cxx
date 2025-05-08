// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "GzipIstream.hxx"
#include "ThreadIstream.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "lib/zlib/Error.hxx"
#include "util/DestructObserver.hxx"

#include <zlib.h>

#include <cassert>

class GzipFilter final : public ThreadIstreamFilter {
	z_stream z;

	SliceFifoBuffer input, output;

	bool z_initialized = false, z_stream_end = false;

public:
	~GzipFilter() noexcept override {
		if (z_initialized)
			deflateEnd(&z);
	}

protected:
	void InitZlib();

	/* virtual methods from class ThreadIstreamFilter */
	void Run(ThreadIstreamInternal &i) override;
	void PostRun(ThreadIstreamInternal &i) noexcept override;

private:
	int GetWindowBits() const noexcept {
		return MAX_WBITS + 16;
	}
};

inline void
GzipFilter::InitZlib()
{
	if (z_initialized)
		return;

	int err = deflateInit2(&z, Z_DEFAULT_COMPRESSION,
			       Z_DEFLATED, GetWindowBits(), 8,
			       Z_DEFAULT_STRATEGY);
	if (err != Z_OK)
		throw MakeZlibError(err, "deflateInit2() failed");

	z_initialized = true;
}

void
GzipFilter::Run(ThreadIstreamInternal &i)
{
	InitZlib();

	int flush = Z_NO_FLUSH;

	{
		const std::scoped_lock lock{i.mutex};
		assert(!i.output.IsNull());
		input.MoveFromAllowBothNull(i.input);

		if (!i.has_input && i.input.empty())
			flush = Z_FINISH;

		i.output.MoveFromAllowNull(output);

		if (output.IsFull()) {
			i.again = true;
			return;
		}
	}

	const auto r = input.Read();
	const auto w = output.Write();

	z.next_in = (Bytef *)reinterpret_cast<Bytef *>(const_cast<std::byte *>(r.data()));
	z.avail_in = static_cast<uInt>(r.size());

	z.next_out = reinterpret_cast<Bytef *>(w.data());
	z.avail_out = static_cast<uInt>(w.size());

	if (int err = deflate(&z, flush); err == Z_STREAM_END)
		z_stream_end = true;
	else if (err != Z_OK)
		throw MakeZlibError(err, "deflate() failed");

	const std::size_t input_consumed = r.size() - static_cast<std::size_t>(z.avail_in);
	input.Consume(input_consumed);
	output.Append(w.size() - static_cast<std::size_t>(z.avail_out));

	{
		const std::scoped_lock lock{i.mutex};
		i.output.MoveFromAllowSrcNull(output);
		i.drained = z_stream_end && output.empty();

		/* run again if:
		   1. our output buffer is full (ThreadIstream will
		      provide a new one)
		   2. there is more input in ThreadIstreamInternal but
		      in this run, there was not enough space in our
		      input buffer, but there is now
		*/
		i.again = w.empty() || (input_consumed > 0 && !i.input.empty());
	}
}

void
GzipFilter::PostRun(ThreadIstreamInternal &) noexcept
{
	input.FreeIfEmpty();
	output.FreeIfEmpty();
}

UnusedIstreamPtr
NewGzipIstream(struct pool &pool, ThreadQueue &queue,
	       UnusedIstreamPtr input) noexcept
{
	return NewThreadIstream(pool, queue, std::move(input),
				std::make_unique<GzipFilter>());

}
