// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

#include "PipeLeaseIstream.hxx"
#include "Result.hxx"
#include "Handler.hxx"
#include "memory/fb_pool.hxx"
#include "io/Buffered.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"

#include <stdexcept>

#include <fcntl.h>

PipeLeaseIstream::~PipeLeaseIstream() noexcept
{
	pipe.Release(remaining == 0 ? PutAction::REUSE : PutAction::DESTROY);
}

bool
PipeLeaseIstream::FeedBuffer() noexcept
{
	auto r = buffer.Read();
	assert(!r.empty());

	size_t consumed = InvokeData(r);
	if (consumed == 0)
		return false;

	buffer.Consume(consumed);
	return buffer.empty();
}

void
PipeLeaseIstream::_Read() noexcept
{
	while (true) {
		/* submit buffer to IstreamHandler */

		if (!buffer.empty() && !FeedBuffer())
			return;

		if (remaining == 0) {
			DestroyEof();
			return;
		}

		assert(pipe.IsDefined());

		if (direct) {
			switch (InvokeDirect(FD_PIPE, pipe.GetReadFd(),
					     IstreamHandler::NO_OFFSET,
					     remaining, true)) {
			case IstreamDirectResult::CLOSED:
			case IstreamDirectResult::BLOCKING:
				return;

			case IstreamDirectResult::END:
				DestroyError(std::make_exception_ptr(std::runtime_error("Premature end of pipe")));
				return;

			case IstreamDirectResult::ERRNO:
				DestroyError(std::make_exception_ptr(MakeErrno("Read from pipe failed")));
				return;

			case IstreamDirectResult::OK:
			case IstreamDirectResult::ASYNC:
				break;
			}
		} else {
			/* fill buffer */

			buffer.AllocateIfNull(fb_pool_get());

			auto nbytes = ReadToBuffer(pipe.GetReadFd(), buffer, remaining);
			assert(nbytes != -2);
			if (nbytes == 0) {
				DestroyError(std::make_exception_ptr(std::runtime_error("Premature end of pipe")));
				return;
			} else if (nbytes == -1) {
				DestroyError(std::make_exception_ptr(MakeErrno("Failed to read from pipe")));
				return;
			}

			assert(nbytes > 0);
			remaining -= nbytes;
		}

		if (remaining == 0)
			pipe.Release(PutAction::REUSE);
	}
}

void
PipeLeaseIstream::_ConsumeDirect(std::size_t nbytes) noexcept
{
	remaining -= nbytes;
}
