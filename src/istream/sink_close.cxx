// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "sink_close.hxx"
#include "Sink.hxx"
#include "UnusedPtr.hxx"
#include "pool/pool.hxx"
#include "io/FileDescriptor.hxx"
#include "util/Compiler.h"

class SinkClose final : IstreamSink {
public:
	explicit SinkClose(UnusedIstreamPtr &&_input)
		:IstreamSink(std::move(_input)) {}

	void Read() noexcept {
		input.Read();
	}

	/* request istream handler */
	std::size_t OnData(std::span<const std::byte>) noexcept {
		CloseInput();
		return 0;
	}

	IstreamDirectResult OnDirect([[maybe_unused]] FdType type,
				     FileDescriptor, off_t,
				     [[maybe_unused]] std::size_t max_length) noexcept {
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
