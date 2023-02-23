// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

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
