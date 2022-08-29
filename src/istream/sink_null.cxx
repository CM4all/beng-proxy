/*
 * Copyright 2007-2022 CM4all GmbH
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

#include "sink_null.hxx"
#include "Sink.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "io/SpliceSupport.hxx"
#include "io/UniqueFileDescriptor.hxx"

#include <fcntl.h> // for splice()

class SinkNull final : IstreamSink {
	UniqueFileDescriptor dev_null;

public:
	explicit SinkNull(UnusedIstreamPtr &&_input)
		:IstreamSink(std::move(_input))
	{
		input.SetDirect(ISTREAM_TO_CHARDEV);
	}

	/* virtual methods from class IstreamHandler */

	std::size_t OnData(const void *, std::size_t length) noexcept override {
		return length;
	}

	ssize_t OnDirect(FdType, int fd, std::size_t max_length) noexcept override
	{
		if (!dev_null.IsDefined())
			if (!dev_null.Open("/dev/null", O_WRONLY))
				return ISTREAM_RESULT_ERRNO;

		return splice(fd, nullptr, dev_null.Get(), nullptr, max_length,
			      SPLICE_F_NONBLOCK | SPLICE_F_MOVE);
	}

	void OnEof() noexcept override {
		ClearInput();
	}

	void OnError(std::exception_ptr) noexcept override {
		ClearInput();
	}
};

void
sink_null_new(struct pool &p, UnusedIstreamPtr istream)
{
	NewFromPool<SinkNull>(p, std::move(istream));
}
