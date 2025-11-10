// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "NullSink.hxx"
#include "Sink.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "io/SpliceSupport.hxx"
#include "io/UniqueFileDescriptor.hxx"

#include <fcntl.h> // for splice()

class NullSink final : IstreamSink {
	UniqueFileDescriptor dev_null;

public:
	explicit NullSink(UnusedIstreamPtr &&_input)
		:IstreamSink(std::move(_input))
	{
		input.SetDirect(ISTREAM_TO_CHARDEV);
	}

private:
	void Destroy() noexcept {
		this->~NullSink();
	}

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(std::span<const std::byte> src) noexcept override {
		return src.size();
	}

	IstreamDirectResult OnDirect(FdType, FileDescriptor fd, off_t offset,
				     std::size_t max_length,
				     [[maybe_unused]] bool then_eof) noexcept override
	{
		if (HasOffset(offset)) {
			/* if there is an offset, we can assume that
			   splicing to /dev/null is a no-op, so we can
			   just omit the call */
			input.ConsumeDirect(max_length);
			return IstreamDirectResult::OK;
		}

		if (!dev_null.IsDefined())
			if (!dev_null.Open("/dev/null", O_WRONLY))
				return IstreamDirectResult::ERRNO;

		const auto nbytes =
			splice(fd.Get(), ToOffsetPointer(offset),
			       dev_null.Get(), nullptr,
			       max_length,
			       SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
		if (nbytes <= 0)
			return nbytes < 0
				? IstreamDirectResult::ERRNO
				: IstreamDirectResult::END;

		input.ConsumeDirect(nbytes);
		return IstreamDirectResult::OK;
	}

	void OnEof() noexcept override {
		ClearInput();
		Destroy();
	}

	void OnError(std::exception_ptr &&) noexcept override {
		ClearInput();
		Destroy();
	}
};

void
NewNullSink(struct pool &p, UnusedIstreamPtr istream)
{
	NewFromPool<NullSink>(p, std::move(istream));
}
