// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "GzipIstream.hxx"
#include "ThreadIstream.hxx"
#include "SimpleThreadIstreamFilter.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "memory/fb_pool.hxx"
#include "memory/SliceFifoBuffer.hxx"
#include "lib/zlib/Error.hxx"
#include "util/DestructObserver.hxx"

#include <zlib.h>

#include <cassert>

class GzipFilter final : public SimpleThreadIstreamFilter {
	z_stream z;

	bool z_initialized = false, z_stream_end = false;

public:
	~GzipFilter() noexcept override {
		if (z_initialized)
			deflateEnd(&z);
	}

protected:
	void InitZlib();

	/* virtual methods from class SimpleThreadIstreamFilter */
	Result SimpleRun(SliceFifoBuffer &input, SliceFifoBuffer &output,
			 Params params) override;

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

SimpleThreadIstreamFilter::Result
GzipFilter::SimpleRun(SliceFifoBuffer &input, SliceFifoBuffer &output,
		      Params params)
{
	InitZlib();

	const int flush = params.finish ? Z_FINISH : Z_NO_FLUSH;

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

	input.Consume(r.size() - static_cast<std::size_t>(z.avail_in));
	output.Append(w.size() - static_cast<std::size_t>(z.avail_out));

	return {
		.drained = z_stream_end,
	};
}

UnusedIstreamPtr
NewGzipIstream(struct pool &pool, ThreadQueue &queue,
	       UnusedIstreamPtr input) noexcept
{
	return NewThreadIstream(pool, queue, std::move(input),
				std::make_unique<GzipFilter>());

}
