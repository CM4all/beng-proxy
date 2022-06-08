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

#include "istream_stopwatch.hxx"
#include "istream/ForwardIstream.hxx"
#include "istream/UnusedPtr.hxx"
#include "istream/New.hxx"
#include "stopwatch.hxx"

class StopwatchIstream final : public ForwardIstream {
	const StopwatchPtr stopwatch;

public:
	StopwatchIstream(struct pool &p, UnusedIstreamPtr _input,
			 StopwatchPtr &&_stopwatch)
		:ForwardIstream(p, std::move(_input)),
		 stopwatch(std::move(_stopwatch)) {}

	/* virtual methods from class Istream */

	int _AsFd() noexcept override;

	/* virtual methods from class IstreamHandler */
	void OnEof() noexcept override;
	void OnError(std::exception_ptr ep) noexcept override;
};


/*
 * istream handler
 *
 */

void
StopwatchIstream::OnEof() noexcept
{
	stopwatch.RecordEvent("end");

	ForwardIstream::OnEof();
}

void
StopwatchIstream::OnError(std::exception_ptr ep) noexcept
{
	stopwatch.RecordEvent("input_error");

	ForwardIstream::OnError(ep);
}

/*
 * istream implementation
 *
 */

int
StopwatchIstream::_AsFd() noexcept
{
	int fd = input.AsFd();
	if (fd >= 0) {
		stopwatch.RecordEvent("as_fd");
		Destroy();
	}

	return fd;
}

/*
 * constructor
 *
 */

UnusedIstreamPtr
istream_stopwatch_new(struct pool &pool, UnusedIstreamPtr input,
		      StopwatchPtr stopwatch)
{
	if (!stopwatch)
		return input;

	return NewIstreamPtr<StopwatchIstream>(pool, std::move(input),
					       std::move(stopwatch));
}
