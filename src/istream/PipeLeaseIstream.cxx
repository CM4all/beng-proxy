/*
 * Copyright 2007-2020 CM4all GmbH
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

#include "PipeLeaseIstream.hxx"
#include "Result.hxx"
#include "fb_pool.hxx"
#include "io/Buffered.hxx"
#include "io/UniqueFileDescriptor.hxx"
#include "system/Error.hxx"

#include <stdexcept>

#include <fcntl.h>

bool
PipeLeaseIstream::FeedBuffer() noexcept
{
	auto r = buffer.Read();
	assert(!r.empty());

	size_t consumed = InvokeData(r.data, r.size);
	if (consumed == 0)
		return false;

	buffer.Consume(consumed);
	return buffer.empty();
}

off_t
PipeLeaseIstream::_Skip(off_t length) noexcept
{
	// TODO: open /dev/null only once
	UniqueFileDescriptor n;
	if (!n.Open("/dev/null", O_WRONLY))
		return -1;

	return splice(pipe.GetReadFd().Get(), nullptr,
		      n.Get(), nullptr,
		      length, SPLICE_F_MOVE|SPLICE_F_NONBLOCK);
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
			auto nbytes = InvokeDirect(FD_PIPE, pipe.GetReadFd().Get(),
						   remaining);
			if (nbytes <= 0) {
				if (nbytes == ISTREAM_RESULT_CLOSED ||
				    nbytes == ISTREAM_RESULT_BLOCKING)
					return;

				if (nbytes == 0) {
					DestroyError(std::make_exception_ptr(std::runtime_error("Premature end of pipe")));
					return;
				}

				DestroyError(std::make_exception_ptr(MakeErrno("Read from pipe failed")));
				return;
			}

			remaining -= nbytes;
		} else {
			/* fill buffer */

			buffer.AllocateIfNull(fb_pool_get());

			auto nbytes = read_to_buffer(pipe.GetReadFd().Get(), buffer, remaining);
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
			pipe.Release(true);
	}
}
