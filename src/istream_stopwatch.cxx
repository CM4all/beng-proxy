// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <max.kellermann@ionos.com>

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

	void _FillBucketList(IstreamBucketList &list) override;
	ConsumeBucketResult _ConsumeBucketList(std::size_t nbytes) noexcept override;

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

void
StopwatchIstream::_FillBucketList(IstreamBucketList &list)
{
	const auto stopwatch2 = stopwatch;

	try {
		ForwardIstream::_FillBucketList(list);
	} catch (...) {
		stopwatch2.RecordEvent("input_error");
		throw;
	}
}

Istream::ConsumeBucketResult
StopwatchIstream::_ConsumeBucketList(std::size_t nbytes) noexcept
{
	const auto c = Consumed(input.ConsumeBucketList(nbytes));
	if (c.eof)
		stopwatch.RecordEvent("end");

	return c;
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
