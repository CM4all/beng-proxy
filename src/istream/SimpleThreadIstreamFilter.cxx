// SPDX-License-Identifier: BSD-2-Clause
// Copyright CM4all GmbH
// author: Max Kellermann <mk@cm4all.com>

#include "SimpleThreadIstreamFilter.hxx"

void
SimpleThreadIstreamFilter::Run(ThreadIstreamInternal &i)
{
	bool finish = false;

	{
		const std::scoped_lock lock{i.mutex};
		unprotected_input.MoveFromAllowBothNull(i.input);

		if (!i.has_input && i.input.empty())
			finish = true;

		i.output.MoveFromAllowNull(unprotected_output);

		if (unprotected_output.IsFull()) {
			i.again = true;
			return;
		}
	}

	const std::size_t input_available = unprotected_input.GetAvailable();

	const auto result = SimpleRun(unprotected_input, unprotected_output,
				      {.finish = finish});

	const bool input_consumed = input_available < unprotected_input.GetAvailable();
	const bool output_full = unprotected_output.IsFull();

	{
		const std::scoped_lock lock{i.mutex};
		i.output.MoveFromAllowSrcNull(unprotected_output);
		i.drained = unprotected_output.empty() && result.drained;

		/* run again if:
		   1. our output buffer is full (ThreadIstream will
		      provide a new one)
		   2. there is more input in ThreadIstreamInternal but
		      in this run, there was not enough space in our
		      input buffer, but there is now
		*/
		i.again = output_full || (input_consumed && !i.input.empty());
	}
}

void
SimpleThreadIstreamFilter::PostRun(ThreadIstreamInternal &) noexcept
{
	unprotected_input.FreeIfEmpty();
	unprotected_output.FreeIfEmpty();
}
