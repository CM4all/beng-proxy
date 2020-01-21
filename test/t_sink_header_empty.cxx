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

#include "IstreamFilterTest.hxx"
#include "istream/sink_header.hxx"
#include "istream/istream.hxx"
#include "istream/DelayedIstream.hxx"
#include "istream/istream_memory.hxx"
#include "istream/UnusedHoldPtr.hxx"
#include "util/Cancellable.hxx"

#include <assert.h>
#include <string.h>

static void
my_sink_header_done(gcc_unused void *header, gcc_unused size_t length,
		    UnusedIstreamPtr tail,
		    void *ctx)
{
	auto &delayed = *(DelayedIstreamControl *)ctx;

	assert(length == 6);
	assert(header != NULL);
	assert(memcmp(header, "foobar", 6) == 0);

	delayed.Set(std::move(tail));
}

static void
my_sink_header_error(std::exception_ptr ep, void *ctx)
{
	auto &delayed = *(DelayedIstreamControl *)ctx;

	delayed.SetError(ep);
}

static const struct sink_header_handler my_sink_header_handler = {
	.done = my_sink_header_done,
	.error = my_sink_header_error,
};

class IstreamSinkHeaderEmptyTestTraits {
public:
	static constexpr const char *expected_result = "";

	static constexpr bool call_available = true;
	static constexpr bool got_data_assert = false;
	static constexpr bool enable_blocking = false;
	static constexpr bool enable_abort_istream = false;

	UnusedIstreamPtr CreateInput(struct pool &pool) const noexcept {
		return istream_memory_new(pool, "\0\0\0\x06" "foobar", 10);
	}

	UnusedIstreamPtr CreateTest(EventLoop &event_loop, struct pool &pool,
				    UnusedIstreamPtr input) const noexcept {
		auto delayed = istream_delayed_new(pool, event_loop);
		UnusedHoldIstreamPtr hold(pool, std::move(delayed.first));

		auto &sink = sink_header_new(pool, std::move(input),
					     my_sink_header_handler, &delayed.second,
					     delayed.second.cancel_ptr);
		sink_header_read(sink);

		return std::move(hold);
	}
};

INSTANTIATE_TYPED_TEST_CASE_P(SinkHeaderEmpty, IstreamFilterTest,
			      IstreamSinkHeaderEmptyTestTraits);
