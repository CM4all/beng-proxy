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

#include "sink_close.hxx"
#include "Sink.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"

class SinkClose final : IstreamSink {
public:
	explicit SinkClose(UnusedIstreamPtr &&_input)
		:IstreamSink(std::move(_input)) {}

	void Read() noexcept {
		input.Read();
	}

	/* request istream handler */
	size_t OnData(gcc_unused const void *data, gcc_unused size_t length) noexcept {
		CloseInput();
		return 0;
	}

	ssize_t OnDirect(gcc_unused FdType type, gcc_unused int fd,
			 gcc_unused size_t max_length) noexcept {
		gcc_unreachable();
	}

	void OnEof() noexcept {
		/* should not be reachable, because we expect the Istream to
		   call the OnData() callback at least once */

		abort();
	}

	void OnError(std::exception_ptr) noexcept {
		/* should not be reachable, because we expect the Istream to
		   call the OnData() callback at least once */

		abort();
	}
};

SinkClose &
sink_close_new(struct pool &p, UnusedIstreamPtr istream)
{
	return *NewFromPool<SinkClose>(p, std::move(istream));
}

void
sink_close_read(SinkClose &sink) noexcept
{
	sink.Read();
}
